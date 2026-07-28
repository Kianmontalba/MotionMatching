#ifndef MM_TRAJECTORY_HPP
#define MM_TRAJECTORY_HPP

#include "feature.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMTrajectory
//
// Predicts where the character wants to be, not where it currently is. The
// predictor is a critically damped spring integrated forward in fixed sub
// steps: it never overshoots, it reacts instantly to a change of intent, and
// releasing the stick decays the future line toward a stop within one
// halflife, which is what makes stop clips get selected on time.
//
// Everything the search consumes is produced in character space so a query
// built while facing north matches a clip authored facing anywhere.
// ---------------------------------------------------------------------------
class MMTrajectory : public RefCounted {
	GDCLASS(MMTrajectory, RefCounted);

private:
	Vector<MMTrajectorySample> _future;
	Vector<MMTrajectorySample> _history;
	PackedFloat32Array _sample_times;

	float _halflife_position = 0.12f;
	float _halflife_direction = 0.10f;
	// How quickly prediction confidence decays with time. Not a measured
	// accuracy -- there is no ground truth for a future that hasn't happened
	// -- but a principled stand-in: the further out a sample is, the more
	// time the player has to change their mind before it, so confidence in
	// it should fall the same way the spring's own correction would if the
	// target moved right now. At t == this halflife, confidence is 50%.
	float _confidence_halflife = 0.5f;
	float _prediction_step = 1.0f / 60.0f;
	float _history_duration = 0.6f;
	float _history_interval = 0.1f;
	float _max_speed = 6.0f;

	// Live state, all in world space.
	Vector3 _position;
	Vector3 _velocity;
	Vector3 _acceleration;
	// Signed radians/sec, +left/-right (Y-up, right-handed). Derived after
	// each update() from the gap between _facing and the first predicted
	// sample rather than tracked as its own spring state -- see update()'s
	// comment at the assignment site for why.
	float _yaw_rate = 0.0f;
	Vector3 _facing = Vector3(0, 0, -1);
	Vector3 _desired_velocity;
	Vector3 _desired_facing = Vector3(0, 0, -1);
	float _history_timer = 0.0f;

	// Environment awareness, filled by the game through the setters below.
	bool _clamp_to_obstacle = false;
	float _obstacle_distance = 0.0f;
	Vector3 _obstacle_normal;

	static void _spring_step(Vector3 &r_value, Vector3 &r_velocity,
			const Vector3 &p_goal, float p_halflife, float p_delta);

protected:
	static void _bind_methods();

public:
	MMTrajectory();

	void configure(const Ref<MMFeatureSchema> &p_schema);
	void set_sample_times(const PackedFloat32Array &p_times);
	PackedFloat32Array get_sample_times() const { return _sample_times; }

	MM_ACCESSORS(float, halflife_position)
	MM_ACCESSORS(float, halflife_direction)
	MM_ACCESSORS(float, confidence_halflife)
	MM_ACCESSORS(float, prediction_step)
	MM_ACCESSORS(float, history_duration)
	MM_ACCESSORS(float, history_interval)
	MM_ACCESSORS(float, max_speed)

	// Input from the character controller, in world space.
	void set_state(const Vector3 &p_position, const Vector3 &p_velocity, const Vector3 &p_facing);
	void set_desired_velocity(const Vector3 &p_velocity);
	void set_desired_facing(const Vector3 &p_facing);

	// Wall awareness: truncates the predicted line at an obstacle instead of
	// letting it push through geometry.
	void set_obstacle(float p_distance, const Vector3 &p_normal);
	void clear_obstacle();

	// Integrates the spring and rebuilds the future samples.
	void update(double p_delta);

	// Overrides the prediction with samples supplied by the game, for AI paths
	// or for a networked replay.
	void set_external_samples(const PackedVector3Array &p_positions,
			const PackedVector3Array &p_directions);

	int get_sample_count() const { return _future.size(); }
	Vector3 get_sample_position(int p_index) const;
	Vector3 get_sample_direction(int p_index) const;
	Vector3 get_sample_velocity(int p_index) const;

	// Live state, for debug panels that want raw numbers rather than
	// derived sample data (current/desired speed, turn intent, and so on).
	Vector3 get_current_velocity() const { return _velocity; }
	Vector3 get_current_facing() const { return _facing; }
	Vector3 get_current_acceleration() const { return _acceleration; }
	float get_current_yaw_rate() const { return _yaw_rate; }
	Vector3 get_desired_velocity() const { return _desired_velocity; }
	Vector3 get_desired_facing() const { return _desired_facing; }

	// Writes the trajectory block of a query vector, in character space.
	void write_features(float *r_query, const Ref<MMFeatureSchema> &p_schema,
			const Basis &p_character_basis) const;

	// Debug draw data: history first, then the current sample, then the
	// future line. All arrays below are the same length and index 1:1 --
	// index i's direction/velocity/time belongs to index i's own position.
	PackedVector3Array get_debug_points() const;
	PackedVector3Array get_debug_directions() const;
	// Actual velocity at each point, not the facing direction -- during a
	// strafe or a swing the two disagree, and that disagreement is exactly
	// what a debug view needs to show instead of hide.
	PackedVector3Array get_debug_velocities() const;
	// Seconds relative to now, negative for history. Paired with the other
	// arrays so a debug draw can label every point with the time (and, by
	// index, the trajectory sample slot) that fed the feature vector.
	PackedFloat32Array get_debug_times() const;
	// 0-1 confidence per point, paired 1:1 with the arrays above. History
	// and "now" are always 1.0 (they already happened); future points decay
	// with time per _confidence_halflife -- see the field comment for what
	// this is and, as importantly, is not.
	PackedFloat32Array get_debug_confidences() const;
};

} // namespace godot

#endif // MM_TRAJECTORY_HPP
