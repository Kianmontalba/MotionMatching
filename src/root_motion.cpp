#include "root_motion.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MMRootMotion::set_database(const Ref<MotionMatchingDatabase> &p_database) {
	_database = p_database;
	reset();
}

// Root motion is integrated from velocity, never from the difference between
// two absolute root transforms. That single decision is what makes a frame
// jump safe: frame 154 of one clip and frame 12 of another have unrelated
// absolute transforms, but comparable velocities.
void MMRootMotion::update(double p_delta, int p_current_frame, int p_previous_frame, float p_blend) {
	if (_database.is_null() || p_current_frame < 0) {
		return;
	}

	const int frames = _database->get_frame_count();
	if (p_current_frame >= frames) {
		return;
	}

	Vector3 linear = _database->get_frame_root_velocity_value(p_current_frame);
	float angular = _database->get_frame_angular_value(p_current_frame);

	// While a transition is in flight the character moves with a mix of both
	// clips, exactly like the pose does.
	if (p_previous_frame >= 0 && p_previous_frame < frames && p_blend < 1.0f) {
		const Vector3 previous_linear = _database->get_frame_root_velocity_value(p_previous_frame);
		const float previous_angular = _database->get_frame_angular_value(p_previous_frame);
		linear = previous_linear.lerp(linear, p_blend);
		angular = Math::lerp(previous_angular, angular, p_blend);
	}

	if (_jump_pending) {
		// Adopt the new clip's velocity immediately rather than easing into
		// it over blend_halflife, so the character does not lag behind the
		// pose it is already showing after a motion-matching switch.
		_linear_velocity = _blend_linear;
		_angular_velocity = _blend_angular;
		_jump_pending = false;
	}

	// Smooth the velocity itself so a hard cut in the database does not become
	// a hard cut in the character controller.
	const float halflife = MAX(_blend_halflife, 0.0001f);
	const float alpha = 1.0f - Math::exp(-0.6931472f * (float)p_delta / halflife);
	_linear_velocity = _linear_velocity.lerp(linear, alpha);
	_angular_velocity = Math::lerp(_angular_velocity, angular, alpha);

	Vector3 translation = _linear_velocity * (float)p_delta;
	if (!_apply_vertical) {
		translation.y = 0.0f;
	}

	// Feed accumulated drift back in slowly. Physics wins the argument, but
	// the animation is allowed to catch up instead of snapping.
	if (_correction_strength > 0.0f && _position_error.length_squared() > 0.0f) {
		const Vector3 correction = _position_error * _correction_strength * (float)p_delta;
		translation += correction;
		_position_error -= correction;
	}

	_accumulated.origin += translation;
	_accumulated.basis = _accumulated.basis.rotated(Vector3(0, 1, 0), _angular_velocity * (float)p_delta);
}

void MMRootMotion::notify_frame_jump(int p_new_frame) {
	if (_database.is_null() || p_new_frame < 0 || p_new_frame >= _database->get_frame_count()) {
		return;
	}
	// Adopt the new clip's velocity immediately rather than easing into it, so
	// the character does not lag behind the pose it is already showing.
	_blend_linear = _database->get_frame_root_velocity_value(p_new_frame);
	_blend_angular = _database->get_frame_angular_value(p_new_frame);
	_jump_pending = true;
}

void MMRootMotion::report_position_error(const Vector3 &p_error) {
	_position_error = p_error;
}

Transform3D MMRootMotion::consume_delta() {
	const Transform3D delta = _accumulated;
	_accumulated = Transform3D();
	return delta;
}

void MMRootMotion::reset() {
	_accumulated = Transform3D();
	_linear_velocity = Vector3();
	_angular_velocity = 0.0f;
	_position_error = Vector3();
	_jump_pending = false;
}

void MMRootMotion::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_database", "database"), &MMRootMotion::set_database);
	ClassDB::bind_method(D_METHOD("get_database"), &MMRootMotion::get_database);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "database", PROPERTY_HINT_RESOURCE_TYPE,
						  "MotionMatchingDatabase"),
			"set_database", "get_database");

	MM_BIND_PROPERTY(MMRootMotion, Variant::FLOAT, blend_halflife)
	MM_BIND_PROPERTY(MMRootMotion, Variant::BOOL, apply_vertical)
	MM_BIND_PROPERTY(MMRootMotion, Variant::FLOAT, correction_strength)

	ClassDB::bind_method(D_METHOD("update", "delta", "current_frame", "previous_frame", "blend"),
			&MMRootMotion::update);
	ClassDB::bind_method(D_METHOD("notify_frame_jump", "frame"), &MMRootMotion::notify_frame_jump);
	ClassDB::bind_method(D_METHOD("report_position_error", "error"), &MMRootMotion::report_position_error);
	ClassDB::bind_method(D_METHOD("get_linear_velocity"), &MMRootMotion::get_linear_velocity);
	ClassDB::bind_method(D_METHOD("get_angular_velocity"), &MMRootMotion::get_angular_velocity);
	ClassDB::bind_method(D_METHOD("consume_delta"), &MMRootMotion::consume_delta);
	ClassDB::bind_method(D_METHOD("peek_delta"), &MMRootMotion::peek_delta);
	ClassDB::bind_method(D_METHOD("reset"), &MMRootMotion::reset);
}
