#include "motion_warping.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// MMMotionWarp
// ---------------------------------------------------------------------------

void MMMotionWarp::begin(const Transform3D &p_start_transform) {
	_start_transform = p_start_transform;
	_accumulated_correction = Transform3D();
	_active = true;
}

void MMMotionWarp::end() {
	_active = false;
	_accumulated_correction = Transform3D();
}

void MMMotionWarp::add_window(float p_start_time, float p_end_time, const Transform3D &p_target,
		bool p_warp_position, bool p_warp_rotation) {
	WarpWindow window;
	window.start_time = p_start_time;
	window.end_time = p_end_time;
	window.target = p_target;
	window.warp_position = p_warp_position;
	window.warp_rotation = p_warp_rotation;
	_windows.push_back(window);
}

void MMMotionWarp::clear_windows() {
	_windows.clear();
}

// The correction is divided by the time left in the window, so it arrives
// exactly when the window closes and never as a visible snap. Limits keep a
// bad target from tearing the character across the level.
Transform3D MMMotionWarp::warp_delta(const Transform3D &p_delta, const Transform3D &p_current,
		float p_time, float p_delta_time) {
	if (!_active || _windows.is_empty() || p_delta_time <= 0.0f) {
		return p_delta;
	}

	Transform3D result = p_delta;

	for (int i = 0; i < _windows.size(); i++) {
		const WarpWindow &window = _windows[i];
		if (p_time < window.start_time || p_time > window.end_time) {
			continue;
		}

		const float remaining = MAX(window.end_time - p_time, 0.0001f);
		const float ratio = MIN(p_delta_time / remaining, 1.0f);

		if (window.warp_position) {
			Vector3 error = window.target.origin - p_current.origin;
			if (error.length() > _translation_limit) {
				error = error.normalized() * _translation_limit;
			}
			result.origin += error * ratio;
		}

		if (window.warp_rotation) {
			const Quaternion current = p_current.basis.get_rotation_quaternion();
			const Quaternion target = window.target.basis.get_rotation_quaternion();
			const Quaternion correction = current.slerp(target, ratio) * current.inverse();
			result.basis = Basis(correction) * result.basis;
		}
	}

	return result;
}

float MMMotionWarp::find_time_for_distance(const PackedFloat32Array &p_distance_curve, float p_distance) {
	// The curve stores remaining travel per sampled frame, so it is monotonic
	// and a linear scan with interpolation is both exact and cheap.
	const int count = p_distance_curve.size();
	if (count == 0) {
		return 0.0f;
	}
	for (int i = 0; i < count - 1; i++) {
		const float a = p_distance_curve[i];
		const float b = p_distance_curve[i + 1];
		if ((p_distance <= a && p_distance >= b) || (p_distance >= a && p_distance <= b)) {
			const float span = a - b;
			const float t = Math::abs(span) > 0.00001f ? (a - p_distance) / span : 0.0f;
			return ((float)i + t) / (float)(count - 1);
		}
	}
	return p_distance > p_distance_curve[0] ? 0.0f : 1.0f;
}

void MMMotionWarp::_bind_methods() {
	MM_BIND_PROPERTY(MMMotionWarp, Variant::FLOAT, translation_limit)
	MM_BIND_PROPERTY(MMMotionWarp, Variant::FLOAT, rotation_limit)

	ClassDB::bind_method(D_METHOD("begin", "start_transform"), &MMMotionWarp::begin);
	ClassDB::bind_method(D_METHOD("end"), &MMMotionWarp::end);
	ClassDB::bind_method(D_METHOD("is_active"), &MMMotionWarp::is_active);
	ClassDB::bind_method(D_METHOD("add_window", "start_time", "end_time", "target", "warp_position",
								 "warp_rotation"),
			&MMMotionWarp::add_window);
	ClassDB::bind_method(D_METHOD("clear_windows"), &MMMotionWarp::clear_windows);
	ClassDB::bind_method(D_METHOD("get_window_count"), &MMMotionWarp::get_window_count);
	ClassDB::bind_method(D_METHOD("warp_delta", "delta", "current", "time", "delta_time"),
			&MMMotionWarp::warp_delta);
	ClassDB::bind_static_method("MMMotionWarp", D_METHOD("find_time_for_distance", "curve", "distance"),
			&MMMotionWarp::find_time_for_distance);
}

// ---------------------------------------------------------------------------
// MMWarpModifier
// ---------------------------------------------------------------------------

void MMWarpModifier::set_spine_bones(const PackedStringArray &p_bones) {
	_spine_bones = p_bones;
	_indices_dirty = true;
}

void MMWarpModifier::set_pelvis_bone(const String &p_bone) {
	_pelvis_bone = p_bone;
	_indices_dirty = true;
}

void MMWarpModifier::_resolve_indices(Skeleton3D *p_skeleton) {
	_spine_indices.resize(_spine_bones.size());
	for (int i = 0; i < _spine_bones.size(); i++) {
		_spine_indices.write[i] = p_skeleton->find_bone(_spine_bones[i]);
	}
	_pelvis_index = p_skeleton->find_bone(_pelvis_bone);
	_indices_dirty = false;
}

// Orientation warping rotates the pelvis toward the direction of travel and
// counter rotates the spine so the upper body keeps facing where the player is
// aiming. It is what lets one forward run clip cover every angle between the
// eight authored directions.
void MMWarpModifier::_process_modification() {
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton == nullptr) {
		return;
	}
	if (_indices_dirty) {
		_resolve_indices(skeleton);
	}

	const float influence = get_influence();
	if (influence <= 0.0f) {
		return;
	}

	// Smoothed so a sudden change of stick direction does not snap the hips.
	const float alpha = 1.0f - Math::exp(-0.6931472f * 0.016f / MAX(_smoothing_halflife, 0.0001f));
	_current_orientation = Math::lerp(_current_orientation, _orientation_angle * _orientation_blend, alpha);
	_current_stride = Math::lerp(_current_stride, _stride_scale, alpha);
	_current_lean = Math::lerp(_current_lean, _lean_amount, alpha);

	const float applied = _current_orientation * influence;

	if (_pelvis_index >= 0 && Math::abs(applied) > 0.0001f) {
		Quaternion rotation = skeleton->get_bone_pose_rotation(_pelvis_index);
		rotation = Quaternion(Vector3(0, 1, 0), applied) * rotation;
		skeleton->set_bone_pose_rotation(_pelvis_index, rotation);

		// Stride warping: scaling the pelvis translation along the travel axis
		// stretches or shortens the step to match the real ground speed.
		if (Math::abs(_current_stride - 1.0f) > 0.001f) {
			Vector3 position = skeleton->get_bone_pose_position(_pelvis_index);
			position.z *= Math::lerp(1.0f, _current_stride, influence);
			skeleton->set_bone_pose_position(_pelvis_index, position);
		}
	}

	const int spine_count = _spine_indices.size();
	if (spine_count > 0 && Math::abs(applied) > 0.0001f) {
		const float per_bone = -applied * _spine_counter_rotation / (float)spine_count;
		for (int i = 0; i < spine_count; i++) {
			const int bone = _spine_indices[i];
			if (bone < 0) {
				continue;
			}
			Quaternion rotation = skeleton->get_bone_pose_rotation(bone);
			rotation = Quaternion(Vector3(0, 1, 0), per_bone) * rotation;

			if (Math::abs(_current_lean) > 0.0001f) {
				const float lean = _current_lean * influence / (float)spine_count;
				rotation = Quaternion(_lean_axis.normalized(), lean) * rotation;
			}
			skeleton->set_bone_pose_rotation(bone, rotation);
		}
	}
}

void MMWarpModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_spine_bones", "bones"), &MMWarpModifier::set_spine_bones);
	ClassDB::bind_method(D_METHOD("get_spine_bones"), &MMWarpModifier::get_spine_bones);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "spine_bones"), "set_spine_bones",
			"get_spine_bones");

	ClassDB::bind_method(D_METHOD("set_pelvis_bone", "bone"), &MMWarpModifier::set_pelvis_bone);
	ClassDB::bind_method(D_METHOD("get_pelvis_bone"), &MMWarpModifier::get_pelvis_bone);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "pelvis_bone"), "set_pelvis_bone", "get_pelvis_bone");

	MM_BIND_PROPERTY(MMWarpModifier, Variant::STRING, left_foot_bone)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::STRING, right_foot_bone)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, orientation_angle)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, orientation_blend)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, spine_counter_rotation)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, stride_scale)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, lean_amount)
	MM_BIND_PROPERTY(MMWarpModifier, Variant::FLOAT, smoothing_halflife)
}
