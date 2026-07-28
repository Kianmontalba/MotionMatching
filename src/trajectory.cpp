#include "trajectory.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

MMTrajectory::MMTrajectory() {
	_sample_times.push_back(0.2f);
	_sample_times.push_back(0.4f);
	_sample_times.push_back(0.6f);
	_sample_times.push_back(1.0f);
	_future.resize(_sample_times.size());
}

// Frame rate independent decay toward a goal. A halflife of 0.12 means the
// remaining error is cut in half every 120 ms, no matter how the game ticks.
void MMTrajectory::_spring_step(Vector3 &r_value, Vector3 &r_velocity, const Vector3 &p_goal,
		float p_halflife, float p_delta) {
	const float halflife = MAX(p_halflife, 0.0001f);
	const float alpha = 1.0f - Math::exp(-0.6931472f * p_delta / halflife);
	const Vector3 previous = r_value;
	r_value = r_value.lerp(p_goal, alpha);
	r_velocity = p_delta > 0.0f ? (r_value - previous) / p_delta : Vector3();
}

void MMTrajectory::configure(const Ref<MMFeatureSchema> &p_schema) {
	if (p_schema.is_null()) {
		return;
	}
	set_sample_times(p_schema->get_trajectory_times());
}

void MMTrajectory::set_sample_times(const PackedFloat32Array &p_times) {
	_sample_times = p_times;
	_future.resize(_sample_times.size());
}

void MMTrajectory::set_state(const Vector3 &p_position, const Vector3 &p_velocity, const Vector3 &p_facing) {
	_position = p_position;
	_velocity = p_velocity;
	if (p_facing.length_squared() > 0.000001f) {
		_facing = p_facing.normalized();
	}
}

void MMTrajectory::set_desired_velocity(const Vector3 &p_velocity) {
	_desired_velocity = p_velocity;
	if (_max_speed > 0.0f && _desired_velocity.length() > _max_speed) {
		_desired_velocity = _desired_velocity.normalized() * _max_speed;
	}
}

void MMTrajectory::set_desired_facing(const Vector3 &p_facing) {
	if (p_facing.length_squared() > 0.000001f) {
		_desired_facing = p_facing.normalized();
	}
}

void MMTrajectory::set_obstacle(float p_distance, const Vector3 &p_normal) {
	_clamp_to_obstacle = true;
	_obstacle_distance = p_distance;
	_obstacle_normal = p_normal;
}

void MMTrajectory::clear_obstacle() {
	_clamp_to_obstacle = false;
	_obstacle_distance = 0.0f;
}

void MMTrajectory::update(double p_delta) {
	const int count = _sample_times.size();
	if (_future.size() != count) {
		_future.resize(count);
	}

	// History, used by the debug draw and by pose history features.
	_history_timer += (float)p_delta;
	if (_history_timer >= _history_interval) {
		_history_timer = 0.0f;
		MMTrajectorySample sample;
		sample.position = _position;
		sample.direction = _facing;
		sample.velocity = _velocity;
		sample.time = -_history_interval;
		_history.insert(0, sample);
		const int max_history = MAX(1, (int)(_history_duration / MAX(_history_interval, 0.001f)));
		while (_history.size() > max_history) {
			_history.remove_at(_history.size() - 1);
		}
	}

	// Forward integration. Sub stepping at a fixed rate keeps the predicted
	// line identical whether the game runs at 30 or 240 fps, which matters for
	// multiplayer determinism.
	Vector3 position = _position;
	Vector3 velocity = _velocity;
	Vector3 acceleration;
	Vector3 facing = _facing;
	Vector3 facing_rate;

	float elapsed = 0.0f;
	int next_sample = 0;
	const float horizon = count > 0 ? _sample_times[count - 1] : 0.0f;
	int guard = 0;

	while (next_sample < count && guard++ < 4096) {
		const float step = MIN(_prediction_step, MAX(_sample_times[next_sample] - elapsed, 0.0001f));

		_spring_step(velocity, acceleration, _desired_velocity, _halflife_position, step);
		position += velocity * step;

		_spring_step(facing, facing_rate, _desired_facing, _halflife_direction, step);
		if (facing.length_squared() > 0.000001f) {
			facing.normalize();
		}

		// Wall awareness: the predicted line stops at geometry instead of
		// pushing through it, so the search never asks for a run clip into a
		// wall it cannot reach.
		if (_clamp_to_obstacle) {
			const Vector3 offset = position - _position;
			const float travelled = Vector3(offset.x, 0.0f, offset.z).length();
			if (travelled > _obstacle_distance) {
				const Vector3 excess = offset - offset.normalized() * _obstacle_distance;
				position -= _obstacle_normal * excess.dot(_obstacle_normal);
			}
		}

		elapsed += step;

		while (next_sample < count && elapsed >= _sample_times[next_sample] - 0.0001f) {
			MMTrajectorySample &sample = _future.write[next_sample];
			sample.position = position;
			sample.direction = facing;
			sample.velocity = velocity;
			sample.time = _sample_times[next_sample];
			next_sample++;
		}

		if (elapsed >= horizon) {
			break;
		}
	}

	_acceleration = acceleration;

	// Signed yaw rate for the feature vector, in radians/sec. Uses the same
	// atan2(x, -z) + wrap-to-[-pi,pi] convention feature_extractor.cpp uses
	// to bake this same feature into the database (see its per-frame
	// angular_velocity computation) -- the two sides must agree on sign and
	// magnitude, or the feature actively misleads the search instead of
	// helping it. Deliberately NOT read from the prediction loop's internal
	// facing_rate above -- that variable is scoped to (and mutated by) the
	// future-sample stepping, and computing this independently keeps it
	// correct even if the prediction loop's stepping changes.
	_yaw_rate = 0.0f;
	if (count > 0 && _future.size() > 0 && _sample_times[0] > 0.0001f) {
		const float yaw_now = Math::atan2(_facing.x, -_facing.z);
		const float yaw_future = Math::atan2(_future[0].direction.x, -_future[0].direction.z);
		float yaw_delta = yaw_future - yaw_now;
		while (yaw_delta > Math_PI) {
			yaw_delta -= Math_TAU;
		}
		while (yaw_delta < -Math_PI) {
			yaw_delta += Math_TAU;
		}
		_yaw_rate = yaw_delta / _sample_times[0];
	}
}

void MMTrajectory::set_external_samples(const PackedVector3Array &p_positions,
		const PackedVector3Array &p_directions) {
	const int count = MIN(p_positions.size(), _sample_times.size());
	if (_future.size() != _sample_times.size()) {
		_future.resize(_sample_times.size());
	}
	for (int i = 0; i < count; i++) {
		MMTrajectorySample &sample = _future.write[i];
		sample.position = p_positions[i];
		sample.direction = i < p_directions.size() ? p_directions[i] : _facing;
		sample.time = _sample_times[i];
	}
}

Vector3 MMTrajectory::get_sample_position(int p_index) const {
	if (p_index < 0 || p_index >= _future.size()) {
		return Vector3();
	}
	return _future[p_index].position;
}

Vector3 MMTrajectory::get_sample_direction(int p_index) const {
	if (p_index < 0 || p_index >= _future.size()) {
		return Vector3(0, 0, -1);
	}
	return _future[p_index].direction;
}

Vector3 MMTrajectory::get_sample_velocity(int p_index) const {
	if (p_index < 0 || p_index >= _future.size()) {
		return Vector3();
	}
	return _future[p_index].velocity;
}

// The query is built in the same character space the database was baked in:
// origin at the character, rotation removed. This is why a clip authored
// facing north matches perfectly while the player faces south.
void MMTrajectory::write_features(float *r_query, const Ref<MMFeatureSchema> &p_schema,
		const Basis &p_character_basis) const {
	if (p_schema.is_null() || r_query == nullptr) {
		return;
	}

	const Basis inverse_basis = p_character_basis.inverse();
	const int position_offset = p_schema->get_trajectory_position_offset();
	const int direction_offset = p_schema->get_trajectory_direction_offset();
	const int count = MIN(p_schema->get_trajectory_point_count(), _future.size());

	for (int i = 0; i < count; i++) {
		const Vector3 local_position = inverse_basis.xform(_future[i].position - _position);
		r_query[position_offset + i * 2 + 0] = local_position.x;
		r_query[position_offset + i * 2 + 1] = local_position.z;

		Vector3 local_direction = inverse_basis.xform(_future[i].direction);
		local_direction.y = 0.0f;
		if (local_direction.length_squared() > 0.000001f) {
			local_direction.normalize();
		} else {
			local_direction = Vector3(0, 0, -1);
		}
		r_query[direction_offset + i * 2 + 0] = local_direction.x;
		r_query[direction_offset + i * 2 + 1] = local_direction.z;
	}

	if (p_schema->get_include_yaw_rate()) {
		r_query[p_schema->get_yaw_rate_offset()] = _yaw_rate;
	}
}

PackedVector3Array MMTrajectory::get_debug_points() const {
	PackedVector3Array points;
	for (int i = _history.size() - 1; i >= 0; i--) {
		points.push_back(_history[i].position);
	}
	points.push_back(_position);
	for (int i = 0; i < _future.size(); i++) {
		points.push_back(_future[i].position);
	}
	return points;
}

PackedVector3Array MMTrajectory::get_debug_directions() const {
	PackedVector3Array directions;
	for (int i = _history.size() - 1; i >= 0; i--) {
		directions.push_back(_history[i].direction);
	}
	directions.push_back(_facing);
	for (int i = 0; i < _future.size(); i++) {
		directions.push_back(_future[i].direction);
	}
	return directions;
}

PackedVector3Array MMTrajectory::get_debug_velocities() const {
	PackedVector3Array velocities;
	for (int i = _history.size() - 1; i >= 0; i--) {
		velocities.push_back(_history[i].velocity);
	}
	velocities.push_back(_velocity);
	for (int i = 0; i < _future.size(); i++) {
		velocities.push_back(_future[i].velocity);
	}
	return velocities;
}

PackedFloat32Array MMTrajectory::get_debug_times() const {
	PackedFloat32Array times;
	for (int i = _history.size() - 1; i >= 0; i--) {
		// History samples are stored nearest-first; walk them back to front
		// so the returned array reads oldest-to-newest like the other
		// debug arrays, with time counting down to zero at "now".
		times.push_back(-_history_interval * (float)(i + 1));
	}
	times.push_back(0.0f);
	for (int i = 0; i < _future.size(); i++) {
		times.push_back(_future[i].time);
	}
	return times;
}

PackedFloat32Array MMTrajectory::get_debug_confidences() const {
	PackedFloat32Array confidences;
	const float halflife = MAX(_confidence_halflife, 0.0001f);
	for (int i = 0; i < _history.size(); i++) {
		confidences.push_back(1.0f);
	}
	confidences.push_back(1.0f); // "now".
	for (int i = 0; i < _future.size(); i++) {
		confidences.push_back(Math::exp(-0.6931472f * _future[i].time / halflife));
	}
	return confidences;
}

void MMTrajectory::_bind_methods() {
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, halflife_position)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, halflife_direction)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, confidence_halflife)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, prediction_step)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, history_duration)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, history_interval)
	MM_BIND_PROPERTY(MMTrajectory, Variant::FLOAT, max_speed)

	ClassDB::bind_method(D_METHOD("set_sample_times", "times"), &MMTrajectory::set_sample_times);
	ClassDB::bind_method(D_METHOD("get_sample_times"), &MMTrajectory::get_sample_times);
	ClassDB::bind_method(D_METHOD("configure", "schema"), &MMTrajectory::configure);
	ClassDB::bind_method(D_METHOD("set_state", "position", "velocity", "facing"), &MMTrajectory::set_state);
	ClassDB::bind_method(D_METHOD("set_desired_velocity", "velocity"), &MMTrajectory::set_desired_velocity);
	ClassDB::bind_method(D_METHOD("set_desired_facing", "facing"), &MMTrajectory::set_desired_facing);
	ClassDB::bind_method(D_METHOD("set_obstacle", "distance", "normal"), &MMTrajectory::set_obstacle);
	ClassDB::bind_method(D_METHOD("clear_obstacle"), &MMTrajectory::clear_obstacle);
	ClassDB::bind_method(D_METHOD("update", "delta"), &MMTrajectory::update);
	ClassDB::bind_method(D_METHOD("set_external_samples", "positions", "directions"),
			&MMTrajectory::set_external_samples);
	ClassDB::bind_method(D_METHOD("get_sample_count"), &MMTrajectory::get_sample_count);
	ClassDB::bind_method(D_METHOD("get_sample_position", "index"), &MMTrajectory::get_sample_position);
	ClassDB::bind_method(D_METHOD("get_sample_direction", "index"), &MMTrajectory::get_sample_direction);
	ClassDB::bind_method(D_METHOD("get_sample_velocity", "index"), &MMTrajectory::get_sample_velocity);
	ClassDB::bind_method(D_METHOD("get_debug_points"), &MMTrajectory::get_debug_points);
	ClassDB::bind_method(D_METHOD("get_debug_directions"), &MMTrajectory::get_debug_directions);
	ClassDB::bind_method(D_METHOD("get_debug_velocities"), &MMTrajectory::get_debug_velocities);
	ClassDB::bind_method(D_METHOD("get_debug_times"), &MMTrajectory::get_debug_times);
	ClassDB::bind_method(D_METHOD("get_debug_confidences"), &MMTrajectory::get_debug_confidences);
	ClassDB::bind_method(D_METHOD("get_current_velocity"), &MMTrajectory::get_current_velocity);
	ClassDB::bind_method(D_METHOD("get_current_facing"), &MMTrajectory::get_current_facing);
	ClassDB::bind_method(D_METHOD("get_current_acceleration"), &MMTrajectory::get_current_acceleration);
	ClassDB::bind_method(D_METHOD("get_current_yaw_rate"), &MMTrajectory::get_current_yaw_rate);
	ClassDB::bind_method(D_METHOD("get_desired_velocity"), &MMTrajectory::get_desired_velocity);
	ClassDB::bind_method(D_METHOD("get_desired_facing"), &MMTrajectory::get_desired_facing);
}
