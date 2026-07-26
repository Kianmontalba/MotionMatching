#include "ik_system.hpp"

#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/physics_ray_query_parameters3d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// MMIKSolver
// ---------------------------------------------------------------------------

void MMIKSolver::set_bone_global_basis(Skeleton3D *p_skeleton, int p_bone, const Basis &p_basis) {
	if (p_skeleton == nullptr || p_bone < 0) {
		return;
	}
	const int parent = p_skeleton->get_bone_parent(p_bone);
	const Basis parent_basis =
			parent >= 0 ? p_skeleton->get_bone_global_pose(parent).basis : Basis();
	const Basis local = parent_basis.inverse() * p_basis;
	p_skeleton->set_bone_pose_rotation(p_bone, local.get_rotation_quaternion());
}

// Analytic two bone IK. The law of cosines gives both joint angles in one
// step, so there is no iteration, no drift and no jitter when the target is
// out of reach: the limb simply straightens.
void MMIKSolver::solve_two_bone(Skeleton3D *p_skeleton, int p_root, int p_mid, int p_tip,
		const Vector3 &p_target, const Vector3 &p_pole, float p_weight) {
	if (p_skeleton == nullptr || p_root < 0 || p_mid < 0 || p_tip < 0 || p_weight <= 0.0f) {
		return;
	}

	const Transform3D root_pose = p_skeleton->get_bone_global_pose(p_root);
	const Transform3D mid_pose = p_skeleton->get_bone_global_pose(p_mid);
	const Transform3D tip_pose = p_skeleton->get_bone_global_pose(p_tip);

	const Vector3 root_position = root_pose.origin;
	const float upper_length = root_position.distance_to(mid_pose.origin);
	const float lower_length = mid_pose.origin.distance_to(tip_pose.origin);
	const float total_length = upper_length + lower_length;

	Vector3 to_target = p_target - root_position;
	float target_distance = to_target.length();
	if (target_distance < 0.0001f) {
		return;
	}

	// Leave a sliver of bend so the limb never locks perfectly straight, which
	// reads as broken on a humanoid.
	const float clamped = CLAMP(target_distance, 0.01f, total_length * 0.999f);
	const Vector3 target_direction = to_target / target_distance;
	const Vector3 clamped_target = root_position + target_direction * clamped;

	// Bend plane: defined by the pole vector, which is what keeps a knee
	// pointing forward instead of sideways.
	Vector3 pole_direction = p_pole - root_position;
	pole_direction -= target_direction * pole_direction.dot(target_direction);
	if (pole_direction.length_squared() < 0.000001f) {
		pole_direction = (mid_pose.origin - root_position);
		pole_direction -= target_direction * pole_direction.dot(target_direction);
	}
	if (pole_direction.length_squared() < 0.000001f) {
		pole_direction = Vector3(0, 0, -1);
	}
	pole_direction.normalize();

	const float cos_root = CLAMP((upper_length * upper_length + clamped * clamped -
										 lower_length * lower_length) /
					(2.0f * upper_length * clamped),
			-1.0f, 1.0f);
	const float root_angle = Math::acos(cos_root);

	const Vector3 bend_axis = target_direction.cross(pole_direction).normalized();
	const Vector3 upper_direction = target_direction.rotated(bend_axis, -root_angle);
	const Vector3 new_mid_position = root_position + upper_direction * upper_length;

	// Build the corrected bases by rotating each bone from where it points now
	// to where it should point.
	const Vector3 current_upper = (mid_pose.origin - root_position).normalized();
	const Quaternion upper_rotation = Quaternion(current_upper, upper_direction);
	Basis root_basis = Basis(upper_rotation.slerp(Quaternion(), 1.0f - p_weight)) * root_pose.basis;
	set_bone_global_basis(p_skeleton, p_root, root_basis);

	const Transform3D updated_mid = p_skeleton->get_bone_global_pose(p_mid);
	const Vector3 current_lower = (p_skeleton->get_bone_global_pose(p_tip).origin - updated_mid.origin)
										  .normalized();
	const Vector3 desired_lower = (clamped_target - new_mid_position).normalized();
	const Quaternion lower_rotation = Quaternion(current_lower, desired_lower);
	Basis mid_basis = Basis(lower_rotation.slerp(Quaternion(), 1.0f - p_weight)) * updated_mid.basis;
	set_bone_global_basis(p_skeleton, p_mid, mid_basis);
}

PackedVector3Array MMIKSolver::solve_fabrik(const PackedVector3Array &p_chain, const Vector3 &p_target,
		int p_iterations, float p_tolerance) {
	PackedVector3Array joints = p_chain;
	const int count = joints.size();
	if (count < 2) {
		return joints;
	}

	PackedFloat32Array lengths;
	lengths.resize(count - 1);
	float total = 0.0f;
	for (int i = 0; i < count - 1; i++) {
		const float length = joints[i].distance_to(joints[i + 1]);
		lengths.set(i, length);
		total += length;
	}

	const Vector3 origin = joints[0];

	// Out of reach: stretch straight at the target and stop.
	if (origin.distance_to(p_target) > total) {
		const Vector3 direction = (p_target - origin).normalized();
		for (int i = 1; i < count; i++) {
			joints.set(i, joints[i - 1] + direction * lengths[i - 1]);
		}
		return joints;
	}

	for (int iteration = 0; iteration < p_iterations; iteration++) {
		if (joints[count - 1].distance_to(p_target) < p_tolerance) {
			break;
		}

		// Backward pass: pin the tip to the target.
		joints.set(count - 1, p_target);
		for (int i = count - 2; i >= 0; i--) {
			const Vector3 direction = (joints[i] - joints[i + 1]).normalized();
			joints.set(i, joints[i + 1] + direction * lengths[i]);
		}

		// Forward pass: pin the root back where it belongs.
		joints.set(0, origin);
		for (int i = 1; i < count; i++) {
			const Vector3 direction = (joints[i] - joints[i - 1]).normalized();
			joints.set(i, joints[i - 1] + direction * lengths[i - 1]);
		}
	}

	return joints;
}

void MMIKSolver::solve_ccd(Skeleton3D *p_skeleton, const PackedInt32Array &p_chain, int p_tip,
		const Vector3 &p_target, int p_iterations, float p_weight) {
	if (p_skeleton == nullptr || p_chain.is_empty() || p_tip < 0) {
		return;
	}

	for (int iteration = 0; iteration < p_iterations; iteration++) {
		for (int i = p_chain.size() - 1; i >= 0; i--) {
			const int bone = p_chain[i];
			if (bone < 0) {
				continue;
			}
			const Transform3D bone_pose = p_skeleton->get_bone_global_pose(bone);
			const Vector3 tip_position = p_skeleton->get_bone_global_pose(p_tip).origin;

			const Vector3 to_tip = tip_position - bone_pose.origin;
			const Vector3 to_target = p_target - bone_pose.origin;
			if (to_tip.length_squared() < 0.000001f || to_target.length_squared() < 0.000001f) {
				continue;
			}

			const Quaternion rotation =
					Quaternion(to_tip.normalized(), to_target.normalized()).slerp(Quaternion(), 1.0f - p_weight);
			set_bone_global_basis(p_skeleton, bone, Basis(rotation) * bone_pose.basis);
		}
	}
}

void MMIKSolver::look_at_bone(Skeleton3D *p_skeleton, int p_bone, const Vector3 &p_target,
		const Vector3 &p_forward_axis, float p_max_angle, float p_weight) {
	if (p_skeleton == nullptr || p_bone < 0 || p_weight <= 0.0f) {
		return;
	}

	const Transform3D pose = p_skeleton->get_bone_global_pose(p_bone);
	Vector3 to_target = p_target - pose.origin;
	if (to_target.length_squared() < 0.000001f) {
		return;
	}
	to_target.normalize();

	const Vector3 current = pose.basis.xform(p_forward_axis).normalized();
	float angle = Math::acos(CLAMP(current.dot(to_target), -1.0f, 1.0f));
	if (angle < 0.0001f) {
		return;
	}

	// Cone limit: past the limit the bone stops turning instead of snapping to
	// an impossible pose.
	const float allowed = MIN(angle, p_max_angle) * p_weight;
	const Vector3 axis = current.cross(to_target).normalized();
	set_bone_global_basis(p_skeleton, p_bone, Basis(Quaternion(axis, allowed)) * pose.basis);
}

void MMIKSolver::_bind_methods() {
	ClassDB::bind_static_method("MMIKSolver",
			D_METHOD("solve_two_bone", "skeleton", "root", "mid", "tip", "target", "pole", "weight"),
			&MMIKSolver::solve_two_bone);
	ClassDB::bind_static_method("MMIKSolver",
			D_METHOD("solve_fabrik", "chain", "target", "iterations", "tolerance"),
			&MMIKSolver::solve_fabrik);
	ClassDB::bind_static_method("MMIKSolver",
			D_METHOD("solve_ccd", "skeleton", "chain", "tip", "target", "iterations", "weight"),
			&MMIKSolver::solve_ccd);
	ClassDB::bind_static_method("MMIKSolver",
			D_METHOD("look_at_bone", "skeleton", "bone", "target", "forward_axis", "max_angle", "weight"),
			&MMIKSolver::look_at_bone);
}

// ---------------------------------------------------------------------------
// MMFootIKModifier
// ---------------------------------------------------------------------------

void MMFootIKModifier::set_foot_contacts(bool p_left, bool p_right) {
	_left_locked = p_left;
	_right_locked = p_right;
}

void MMFootIKModifier::_resolve_indices(Skeleton3D *p_skeleton) {
	_indices[0] = p_skeleton->find_bone(_left_upper_leg);
	_indices[1] = p_skeleton->find_bone(_left_lower_leg);
	_indices[2] = p_skeleton->find_bone(_left_foot);
	_indices[3] = p_skeleton->find_bone(_right_upper_leg);
	_indices[4] = p_skeleton->find_bone(_right_lower_leg);
	_indices[5] = p_skeleton->find_bone(_right_foot);
	_indices[6] = p_skeleton->find_bone(_pelvis);
	_indices_dirty = false;

	bool any_resolved = false;
	for (int i = 0; i < 7; i++) {
		if (_indices[i] >= 0) {
			any_resolved = true;
			break;
		}
	}
	if (!any_resolved) {
		WARN_PRINT_ONCE(vformat(
				"MMFootIKModifier could not find any of its configured bone names on skeleton "
				"'%s' (left_upper_leg='%s', left_lower_leg='%s', left_foot='%s', "
				"right_upper_leg='%s', right_lower_leg='%s', right_foot='%s', pelvis='%s'). "
				"Foot IK will silently do nothing until the bone names match the skeleton.",
				p_skeleton->get_name(), _left_upper_leg, _left_lower_leg, _left_foot,
				_right_upper_leg, _right_lower_leg, _right_foot, _pelvis));
	}
}

// Turns the seven plain bone-name fields into a searchable dropdown of the
// actual skeleton's bones once one is resolvable, instead of requiring the
// name to be typed by hand. PROPERTY_HINT_ENUM_SUGGESTION (not
// PROPERTY_HINT_ENUM) is deliberate: it offers the skeleton's bone names as
// suggestions but still accepts any text, so a name that doesn't match any
// bone yet (the modifier added before the skeleton is wired up, a bone
// renamed later, ...) doesn't get silently clobbered.
void MMFootIKModifier::_validate_property(PropertyInfo &p_property) const {
	static const StringName bone_property_names[] = {
		StringName("left_upper_leg"), StringName("left_lower_leg"), StringName("left_foot"),
		StringName("right_upper_leg"), StringName("right_lower_leg"), StringName("right_foot"),
		StringName("pelvis")
	};
	bool is_bone_property = false;
	for (const StringName &name : bone_property_names) {
		if (p_property.name == name) {
			is_bone_property = true;
			break;
		}
	}
	if (!is_bone_property) {
		return;
	}

	// Only meaningful once this modifier is actually under a Skeleton3D --
	// before that (freshly added, not yet reparented) the field just stays
	// a plain text box, same as it always was.
	Skeleton3D *skeleton = const_cast<MMFootIKModifier *>(this)->get_skeleton();
	if (skeleton == nullptr) {
		return;
	}

	p_property.hint = PROPERTY_HINT_ENUM_SUGGESTION;
	p_property.hint_string = skeleton->get_concatenated_bone_names();
}

// Order matters. The pelvis is lowered first so both legs solve against a
// reachable target; solving the legs first and then moving the pelvis would
// undo the solve.
void MMFootIKModifier::_process_modification() {
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

	Ref<World3D> world = skeleton->get_world_3d();
	if (world.is_null()) {
		return;
	}
	PhysicsDirectSpaceState3D *space = world->get_direct_space_state();
	if (space == nullptr) {
		return;
	}

	const Transform3D skeleton_transform = skeleton->get_global_transform();
	const Vector3 up = Vector3(0, 1, 0);
	const float alpha = 1.0f - Math::exp(-0.6931472f * get_process_delta_time() / MAX(_smoothing_halflife, 0.0001f));

	float targets[2] = { 0.0f, 0.0f };
	Vector3 world_targets[2];
	Vector3 world_normals[2] = { up, up };
	bool grounded[2] = { false, false };

	for (int side = 0; side < 2; side++) {
		const int foot = _indices[side == 0 ? 2 : 5];
		if (foot < 0) {
			continue;
		}

		const Vector3 foot_world = skeleton_transform.xform(skeleton->get_bone_global_pose(foot).origin);
		const Vector3 from = foot_world + up * _ray_start_height;
		const Vector3 to = foot_world - up * _ray_length;

		Ref<PhysicsRayQueryParameters3D> query = PhysicsRayQueryParameters3D::create(from, to);
		query->set_collision_mask(_collision_mask);
		const Dictionary hit = space->intersect_ray(query);
		if (hit.is_empty()) {
			continue;
		}

		const Vector3 position = hit["position"];
		const Vector3 normal = hit["normal"];

		// Refuse to plant on a wall.
		if (Math::acos(CLAMP(normal.dot(up), -1.0f, 1.0f)) > _max_slope_angle) {
			continue;
		}

		grounded[side] = true;
		world_normals[side] = normal;
		world_targets[side] = position + normal * _foot_offset;
		targets[side] = CLAMP(world_targets[side].y - foot_world.y, -_max_step_height, _max_step_height);
	}

	// The pelvis follows the lower foot so the standing leg never has to
	// stretch past its length.
	float desired_pelvis = 0.0f;
	if (grounded[0] || grounded[1]) {
		const float lowest = MIN(grounded[0] ? targets[0] : 0.0f, grounded[1] ? targets[1] : 0.0f);
		desired_pelvis = MIN(lowest, 0.0f) * _pelvis_adjust_strength;
	}
	_pelvis_offset = Math::lerp(_pelvis_offset, desired_pelvis, alpha);

	const int pelvis = _indices[6];
	if (pelvis >= 0 && Math::abs(_pelvis_offset) > 0.0001f) {
		Vector3 position = skeleton->get_bone_pose_position(pelvis);
		position.y += _pelvis_offset * influence;
		skeleton->set_bone_pose_position(pelvis, position);
	}

	_left_offset = Math::lerp(_left_offset, grounded[0] ? targets[0] : 0.0f, alpha);
	_right_offset = Math::lerp(_right_offset, grounded[1] ? targets[1] : 0.0f, alpha);

	for (int side = 0; side < 2; side++) {
		if (!grounded[side]) {
			continue;
		}

		const int upper = _indices[side == 0 ? 0 : 3];
		const int lower = _indices[side == 0 ? 1 : 4];
		const int foot = _indices[side == 0 ? 2 : 5];
		if (upper < 0 || lower < 0 || foot < 0) {
			continue;
		}

		const Transform3D inverse_skeleton = skeleton_transform.affine_inverse();
		Vector3 local_target = inverse_skeleton.xform(world_targets[side]);

		// A locked foot ignores the animation's own translation for as long as
		// the database says the foot is planted. This is what removes sliding.
		const bool locked = side == 0 ? _left_locked : _right_locked;
		if (locked) {
			Vector3 &lock = side == 0 ? _left_lock_position : _right_lock_position;
			if (lock == Vector3()) {
				lock = local_target;
			}
			local_target = local_target.lerp(lock, influence);
		} else {
			(side == 0 ? _left_lock_position : _right_lock_position) = Vector3();
		}

		// The knee points where the foot points.
		const Transform3D knee_pose = skeleton->get_bone_global_pose(lower);
		const Vector3 pole = knee_pose.origin + skeleton->get_bone_global_pose(upper).basis.xform(
																 Vector3(0, 0, -1)) *
													  1.0f;

		MMIKSolver::solve_two_bone(skeleton, upper, lower, foot, local_target, pole, influence);

		if (_adapt_rotation) {
			const Transform3D foot_pose = skeleton->get_bone_global_pose(foot);
			const Vector3 local_normal = inverse_skeleton.basis.xform(world_normals[side]).normalized();
			const Vector3 foot_up = foot_pose.basis.xform(Vector3(0, 1, 0)).normalized();

			// Raw ground-normal alignment, clamped to a plausible ankle range
			// so a steep or momentarily noisy ground reading cannot twist the
			// foot further than an ankle actually could.
			Quaternion target_align = Quaternion(foot_up, local_normal);
			if (target_align.get_angle() > _max_foot_rotation_angle) {
				target_align = Quaternion(target_align.get_axis(), _max_foot_rotation_angle);
			}

			// Eased toward that target instead of snapped to it, same
			// exponential-halflife spring used for the pelvis/step offsets
			// above -- this is what makes a foot settle onto a slanted rock
			// softly, the way an ankle actually responds, instead of
			// twisting into place the instant the ray hits.
			const float rotation_alpha = 1.0f - Math::exp(-0.6931472f * get_process_delta_time() /
					MAX(_rotation_smoothing_halflife, 0.0001f));
			Quaternion &smoothed = side == 0 ? _left_foot_align : _right_foot_align;
			smoothed = smoothed.slerp(target_align, rotation_alpha).normalized();

			const Quaternion align = smoothed.slerp(Quaternion(), 1.0f - influence);
			// Fixed per-rig calibration, applied in the foot's own local
			// space on top of the (already soft) dynamic alignment above --
			// this is what lines the sole up with the mesh when the bone's
			// own axes don't already agree with it.
			const Basis offset_basis =
					Basis(Vector3(1, 0, 0), Math::deg_to_rad(_foot_rotation_offset_degrees.x)) *
					Basis(Vector3(0, 1, 0), Math::deg_to_rad(_foot_rotation_offset_degrees.y)) *
					Basis(Vector3(0, 0, 1), Math::deg_to_rad(_foot_rotation_offset_degrees.z));
			MMIKSolver::set_bone_global_basis(skeleton, foot, Basis(align) * foot_pose.basis * offset_basis);
		}
	}
}

void MMFootIKModifier::_bind_methods() {
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, left_upper_leg)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, left_lower_leg)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, left_foot)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, right_upper_leg)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, right_lower_leg)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, right_foot)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::STRING, pelvis)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, ray_start_height)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, ray_length)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, foot_offset)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, max_step_height)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, pelvis_adjust_strength)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, smoothing_halflife)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::BOOL, adapt_rotation)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, max_foot_rotation_angle)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, rotation_smoothing_halflife)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::VECTOR3, foot_rotation_offset_degrees)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::FLOAT, max_slope_angle)
	MM_BIND_PROPERTY(MMFootIKModifier, Variant::INT, collision_mask)

	ClassDB::bind_method(D_METHOD("set_foot_contacts", "left", "right"),
			&MMFootIKModifier::set_foot_contacts);
}

// ---------------------------------------------------------------------------
// MMAimIKModifier
// ---------------------------------------------------------------------------

void MMAimIKModifier::set_chain_bones(const PackedStringArray &p_bones) {
	_chain_bones = p_bones;
	_indices_dirty = true;
}

void MMAimIKModifier::_process_modification() {
	Skeleton3D *skeleton = get_skeleton();
	if (skeleton == nullptr || _chain_bones.is_empty()) {
		return;
	}

	if (_indices_dirty) {
		_indices.resize(_chain_bones.size());
		bool any_resolved = false;
		for (int i = 0; i < _chain_bones.size(); i++) {
			_indices.write[i] = skeleton->find_bone(_chain_bones[i]);
			if (_indices[i] >= 0) {
				any_resolved = true;
			}
		}
		if (!any_resolved) {
			WARN_PRINT_ONCE(vformat(
					"MMAimIKModifier could not find any of its configured chain_bones on "
					"skeleton '%s'. Aim IK will silently do nothing until the bone names match "
					"the skeleton.",
					skeleton->get_name()));
		}
		_indices_dirty = false;
	}

	const float influence = get_influence();
	if (influence <= 0.0f) {
		return;
	}

	const float alpha = 1.0f - Math::exp(-0.6931472f * get_process_delta_time() / MAX(_smoothing_halflife, 0.0001f));
	_current_target = _current_target.lerp(_target, alpha);

	const Transform3D inverse_skeleton = skeleton->get_global_transform().affine_inverse();
	const Vector3 local_target = inverse_skeleton.xform(_current_target);

	// Spreading the rotation over the chain is what makes an aim offset read
	// as a body turning rather than a head snapping.
	const int count = _indices.size();
	for (int i = 0; i < count; i++) {
		const int bone = _indices[i];
		if (bone < 0) {
			continue;
		}
		const float weight = i < _bone_weights.size() ? _bone_weights[i] : 1.0f / (float)count;
		MMIKSolver::look_at_bone(skeleton, bone, local_target, _forward_axis, _max_angle,
				weight * influence);
	}
}

void MMAimIKModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_chain_bones", "bones"), &MMAimIKModifier::set_chain_bones);
	ClassDB::bind_method(D_METHOD("get_chain_bones"), &MMAimIKModifier::get_chain_bones);
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "chain_bones"), "set_chain_bones",
			"get_chain_bones");

	MM_BIND_PROPERTY(MMAimIKModifier, Variant::PACKED_FLOAT32_ARRAY, bone_weights)
	MM_BIND_PROPERTY(MMAimIKModifier, Variant::VECTOR3, target)
	MM_BIND_PROPERTY(MMAimIKModifier, Variant::VECTOR3, forward_axis)
	MM_BIND_PROPERTY(MMAimIKModifier, Variant::FLOAT, max_angle)
	MM_BIND_PROPERTY(MMAimIKModifier, Variant::FLOAT, smoothing_halflife)
}
