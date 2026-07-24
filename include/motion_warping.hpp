#ifndef MM_MOTION_WARPING_HPP
#define MM_MOTION_WARPING_HPP

#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/skeleton_modifier3d.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMMotionWarp
//
// Bends a clip toward a target it was not authored for. A vault animation is
// captured against one obstacle; the game has a thousand. Warping closes that
// gap without needing a thousand clips.
//
// The correction is spread over a window rather than applied at once, and it
// is applied to the root delta, so the pose itself is never distorted.
// ---------------------------------------------------------------------------
class MMMotionWarp : public RefCounted {
	GDCLASS(MMMotionWarp, RefCounted);

public:
	struct WarpWindow {
		float start_time = 0.0f;
		float end_time = 0.0f;
		Transform3D target;
		bool warp_position = true;
		bool warp_rotation = true;
	};

private:
	Vector<WarpWindow> _windows;
	Transform3D _start_transform;
	Transform3D _accumulated_correction;
	bool _active = false;
	float _translation_limit = 2.5f;
	float _rotation_limit = 3.14159f;

protected:
	static void _bind_methods();

public:
	void begin(const Transform3D &p_start_transform);
	void end();
	bool is_active() const { return _active; }

	void add_window(float p_start_time, float p_end_time, const Transform3D &p_target,
			bool p_warp_position = true, bool p_warp_rotation = true);
	void clear_windows();
	int get_window_count() const { return _windows.size(); }

	MM_ACCESSORS(float, translation_limit)
	MM_ACCESSORS(float, rotation_limit)

	// Returns the corrected delta for this tick. p_time is the playback time
	// inside the warped clip; p_remaining is the animation's own remaining
	// motion toward the window's end, used to compute how much still has to
	// be made up.
	Transform3D warp_delta(const Transform3D &p_delta, const Transform3D &p_current,
			float p_time, float p_delta_time);

	// Distance matching: returns the playback time whose remaining root travel
	// best matches p_distance. Used to start a stop or a takeoff at exactly
	// the right moment.
	static float find_time_for_distance(const PackedFloat32Array &p_distance_curve, float p_distance);
};

// ---------------------------------------------------------------------------
// MMWarpModifier
//
// Pose level warping applied after the animation has been sampled:
//
//   orientation warping  the character runs at 40 degrees off the clip's
//                        forward without a dedicated diagonal clip
//   stride warping       step length scales with actual speed, which is the
//                        single biggest cause of foot sliding
//   lean                 procedural upper body response to acceleration
// ---------------------------------------------------------------------------
class MMWarpModifier : public SkeletonModifier3D {
	GDCLASS(MMWarpModifier, SkeletonModifier3D);

private:
	PackedStringArray _spine_bones;
	String _pelvis_bone = "Hips";
	String _left_foot_bone = "LeftFoot";
	String _right_foot_bone = "RightFoot";

	float _orientation_angle = 0.0f;
	float _orientation_blend = 1.0f;
	float _spine_counter_rotation = 0.5f;
	float _stride_scale = 1.0f;
	float _lean_amount = 0.0f;
	Vector3 _lean_axis = Vector3(1, 0, 0);
	float _smoothing_halflife = 0.08f;

	float _current_orientation = 0.0f;
	float _current_stride = 1.0f;
	float _current_lean = 0.0f;

	Vector<int> _spine_indices;
	int _pelvis_index = -1;
	bool _indices_dirty = true;

	void _resolve_indices(Skeleton3D *p_skeleton);

protected:
	static void _bind_methods();

public:
	void set_spine_bones(const PackedStringArray &p_bones);
	PackedStringArray get_spine_bones() const { return _spine_bones; }

	void set_pelvis_bone(const String &p_bone);
	String get_pelvis_bone() const { return _pelvis_bone; }

	MM_ACCESSORS(String, left_foot_bone)
	MM_ACCESSORS(String, right_foot_bone)
	MM_ACCESSORS(float, orientation_angle)
	MM_ACCESSORS(float, orientation_blend)
	MM_ACCESSORS(float, spine_counter_rotation)
	MM_ACCESSORS(float, stride_scale)
	MM_ACCESSORS(float, lean_amount)
	MM_ACCESSORS(float, smoothing_halflife)

	void _process_modification() override;
};

} // namespace godot

#endif // MM_MOTION_WARPING_HPP
