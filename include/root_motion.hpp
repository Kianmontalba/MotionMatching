#ifndef MM_ROOT_MOTION_HPP
#define MM_ROOT_MOTION_HPP

#include "frame_database.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMRootMotion
//
// Produces the per tick delta transform that the CharacterBody3D should apply,
// and survives frame jumps.
//
// The rule that keeps a motion matching character from teleporting: root
// motion is only ever integrated from velocities, never from the difference
// between two absolute root transforms. When the search jumps from frame 154
// of one clip to frame 12 of another, the absolute transforms are unrelated,
// but the velocities are comparable, so the delta stays continuous.
// ---------------------------------------------------------------------------
class MMRootMotion : public RefCounted {
	GDCLASS(MMRootMotion, RefCounted);

private:
	Ref<MotionMatchingDatabase> _database;

	Vector3 _linear_velocity;
	float _angular_velocity = 0.0f;
	Vector3 _blend_linear;
	float _blend_angular = 0.0f;
	bool _jump_pending = false;

	float _blend_halflife = 0.08f;
	float _blend_weight = 1.0f;
	bool _apply_vertical = false;
	float _correction_strength = 0.0f;
	Vector3 _position_error;

	Transform3D _accumulated;

protected:
	static void _bind_methods();

public:
	void set_database(const Ref<MotionMatchingDatabase> &p_database);
	Ref<MotionMatchingDatabase> get_database() const { return _database; }

	MM_ACCESSORS(float, blend_halflife)
	MM_ACCESSORS(bool, apply_vertical)
	MM_ACCESSORS(float, correction_strength)

	// Called every tick with the frame currently playing and the blend weight
	// between the outgoing and incoming clip.
	void update(double p_delta, int p_current_frame, int p_previous_frame, float p_blend);

	// Called on a frame jump so the next delta is not polluted by the clip we
	// just left.
	void notify_frame_jump(int p_new_frame);

	// Accumulates the drift between where root motion wanted the character and
	// where physics actually put it, then feeds it back over time.
	void report_position_error(const Vector3 &p_error);

	Vector3 get_linear_velocity() const { return _linear_velocity; }
	float get_angular_velocity() const { return _angular_velocity; }

	// Delta for this tick, in character space. Consuming it clears it.
	Transform3D consume_delta();
	Transform3D peek_delta() const { return _accumulated; }
	void reset();
};

} // namespace godot

#endif // MM_ROOT_MOTION_HPP
