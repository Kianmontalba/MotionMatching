#ifndef MM_IK_SYSTEM_HPP
#define MM_IK_SYSTEM_HPP

#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/skeleton_modifier3d.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMIKSolver
//
// Stateless solvers. They are exposed so gameplay code, traversal and the
// modifiers below can all share one implementation instead of three.
// ---------------------------------------------------------------------------
class MMIKSolver : public RefCounted {
	GDCLASS(MMIKSolver, RefCounted);

protected:
	static void _bind_methods();

public:
	// Analytic two bone solve: exact, one iteration, no drift. This is the
	// right solver for limbs, and the only one that should touch legs.
	static void solve_two_bone(Skeleton3D *p_skeleton, int p_root, int p_mid, int p_tip,
			const Vector3 &p_target, const Vector3 &p_pole, float p_weight);

	// Iterative solvers for chains longer than two bones: spines, tails,
	// tentacles, ledge grabs with a shifting anchor.
	static PackedVector3Array solve_fabrik(const PackedVector3Array &p_chain, const Vector3 &p_target,
			int p_iterations, float p_tolerance);
	static void solve_ccd(Skeleton3D *p_skeleton, const PackedInt32Array &p_chain, int p_tip,
			const Vector3 &p_target, int p_iterations, float p_weight);

	// Rotates a bone toward a target with a cone limit, used for look and aim.
	static void look_at_bone(Skeleton3D *p_skeleton, int p_bone, const Vector3 &p_target,
			const Vector3 &p_forward_axis, float p_max_angle, float p_weight);

	// Applies a model space rotation to a bone by converting it into the
	// bone's parent space first.
	static void set_bone_global_basis(Skeleton3D *p_skeleton, int p_bone, const Basis &p_basis);
};

// ---------------------------------------------------------------------------
// MMFootIKModifier
//
// Ground adaptation. Feet stop floating on stairs and stop sinking on slopes,
// and the pelvis drops far enough that the standing leg never over extends.
//
// Foot locking uses the contact flags baked into the database, so a planted
// foot stays planted for exactly as long as the animator planted it.
// ---------------------------------------------------------------------------
class MMFootIKModifier : public SkeletonModifier3D {
	GDCLASS(MMFootIKModifier, SkeletonModifier3D);

private:
	String _left_upper_leg = "LeftUpperLeg";
	String _left_lower_leg = "LeftLowerLeg";
	String _left_foot = "LeftFoot";
	String _right_upper_leg = "RightUpperLeg";
	String _right_lower_leg = "RightLowerLeg";
	String _right_foot = "RightFoot";
	String _pelvis = "Hips";

	float _ray_start_height = 0.6f;
	float _ray_length = 1.2f;
	float _foot_offset = 0.02f;
	float _max_step_height = 0.55f;
	float _pelvis_adjust_strength = 1.0f;
	float _smoothing_halflife = 0.08f;
	bool _adapt_rotation = true;
	// How far the foot is allowed to tilt off flat to match the ground
	// normal, and how softly it eases there. Kept separate from
	// _smoothing_halflife (which times the foot's vertical offset) because
	// a natural-feeling ankle roll and a natural-feeling step height rarely
	// want the same timing.
	float _max_foot_rotation_angle = 0.6109f; // radians, roughly 35 degrees
	float _rotation_smoothing_halflife = 0.10f;
	// Static calibration, applied on top of the smoothed ground alignment
	// every tick: some rigs' foot bone axes don't sit flush with the mesh's
	// own sole/flat orientation, so the adaptation looks tilted even when
	// it's mathematically correct. This corrects for that per rig, once,
	// rather than needing the mesh or the rest pose changed.
	Vector3 _foot_rotation_offset_degrees = Vector3(0, 0, 0);
	float _max_slope_angle = 0.87f; // radians, roughly 50 degrees
	uint32_t _collision_mask = 1;

	float _left_offset = 0.0f;
	float _right_offset = 0.0f;
	float _pelvis_offset = 0.0f;
	bool _left_locked = false;
	bool _right_locked = false;
	Vector3 _left_lock_position;
	Vector3 _right_lock_position;
	// Smoothed ground-alignment rotation per foot, eased toward the raw
	// (clamped) target each tick instead of snapping straight to it -- this
	// is what keeps a foot planting on a slanted or noisy surface from
	// twisting there instantly.
	Quaternion _left_foot_align;
	Quaternion _right_foot_align;

	int _indices[7] = { -1, -1, -1, -1, -1, -1, -1 };
	bool _indices_dirty = true;

	void _resolve_indices(Skeleton3D *p_skeleton);
	bool _solve_leg(Skeleton3D *p_skeleton, int p_upper, int p_lower, int p_foot, bool p_locked,
			const Vector3 &p_lock_position, float &r_offset, float p_delta);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	MM_ACCESSORS(String, left_upper_leg)
	MM_ACCESSORS(String, left_lower_leg)
	MM_ACCESSORS(String, left_foot)
	MM_ACCESSORS(String, right_upper_leg)
	MM_ACCESSORS(String, right_lower_leg)
	MM_ACCESSORS(String, right_foot)
	MM_ACCESSORS(String, pelvis)
	MM_ACCESSORS(float, ray_start_height)
	MM_ACCESSORS(float, ray_length)
	MM_ACCESSORS(float, foot_offset)
	MM_ACCESSORS(float, max_step_height)
	MM_ACCESSORS(float, pelvis_adjust_strength)
	MM_ACCESSORS(float, smoothing_halflife)
	MM_ACCESSORS(bool, adapt_rotation)
	MM_ACCESSORS(float, max_foot_rotation_angle)
	MM_ACCESSORS(float, rotation_smoothing_halflife)
	MM_ACCESSORS(Vector3, foot_rotation_offset_degrees)
	MM_ACCESSORS(float, max_slope_angle)

	void set_collision_mask(int p_mask) { _collision_mask = (uint32_t)p_mask; }
	int get_collision_mask() const { return (int)_collision_mask; }

	// Driven from the database contact flags each tick.
	void set_foot_contacts(bool p_left, bool p_right);

	void _process_modification() override;
};

// ---------------------------------------------------------------------------
// MMAimIKModifier
//
// Distributes an aim or look rotation across a chain, weighted per bone, with
// a cone limit so the character never breaks its own neck. Runs after the
// warp modifier so the upper body aims from its already corrected orientation.
// ---------------------------------------------------------------------------
class MMAimIKModifier : public SkeletonModifier3D {
	GDCLASS(MMAimIKModifier, SkeletonModifier3D);

private:
	PackedStringArray _chain_bones;
	PackedFloat32Array _bone_weights;
	Vector3 _target;
	Vector3 _forward_axis = Vector3(0, 0, -1);
	float _max_angle = 1.2f;
	float _smoothing_halflife = 0.06f;
	bool _use_target_node = false;

	Vector3 _current_target;
	Vector<int> _indices;
	bool _indices_dirty = true;

protected:
	static void _bind_methods();

public:
	void set_chain_bones(const PackedStringArray &p_bones);
	PackedStringArray get_chain_bones() const { return _chain_bones; }

	MM_ACCESSORS(PackedFloat32Array, bone_weights)
	MM_ACCESSORS(Vector3, target)
	MM_ACCESSORS(Vector3, forward_axis)
	MM_ACCESSORS(float, max_angle)
	MM_ACCESSORS(float, smoothing_halflife)

	void _process_modification() override;
};

} // namespace godot

#endif // MM_IK_SYSTEM_HPP
