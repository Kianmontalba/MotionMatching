#include "feature.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// The layout is ordered cheapest first so a quality level is a prefix of the
// full vector and LOD costs nothing but a smaller loop bound:
//
//   [trajectory position][trajectory direction][yaw rate][root velocity]  <- LOW
//   [bone positions]                                            <- MEDIUM
//   [bone velocities]                                           <- HIGH
//   [extra]                                                     <- ULTRA

MMFeatureSchema::MMFeatureSchema() {
	_trajectory_times.push_back(0.2f);
	_trajectory_times.push_back(0.4f);
	_trajectory_times.push_back(0.6f);
	_trajectory_times.push_back(1.0f);

	// No bone names are assumed. The list is filled by apply_skeleton_profile()
	// once an actual skeleton is available, so the schema is portable across
	// every naming convention.
	_skeleton_profile.instantiate();

	_rebuild_layout();
}

void MMFeatureSchema::_rebuild_layout() {
	const int points = _trajectory_times.size();
	const int bones = _pose_bones.size();

	int offset = 0;
	_offset_trajectory_position = offset;
	offset += points * 2;

	_offset_trajectory_direction = offset;
	offset += points * 2;

	_offset_yaw_rate = offset;
	if (_include_yaw_rate) {
		offset += 1;
	}

	_offset_root_velocity = offset;
	if (_include_root_velocity) {
		offset += 3;
	}

	_offset_bone_position = offset;
	offset += bones * 3;

	_offset_bone_velocity = offset;
	if (_include_bone_velocity) {
		offset += bones * 3;
	}

	_offset_extra = offset;
	if (_extra_dimensions > 0) {
		offset += _extra_dimensions;
	}

	_dimension = offset;

	_dimension_groups.resize(_dimension);
	uint8_t *groups = _dimension_groups.ptrw();
	for (int i = 0; i < _dimension; i++) {
		if (i < _offset_trajectory_direction) {
			groups[i] = MM_GROUP_TRAJECTORY_POSITION;
		} else if (i < _offset_yaw_rate) {
			groups[i] = MM_GROUP_TRAJECTORY_DIRECTION;
		} else if (i < _offset_root_velocity) {
			groups[i] = MM_GROUP_YAW_RATE;
		} else if (i < _offset_bone_position) {
			groups[i] = MM_GROUP_ROOT_VELOCITY;
		} else if (i < _offset_bone_velocity) {
			groups[i] = MM_GROUP_POSE_POSITION;
		} else if (i < _offset_extra) {
			groups[i] = MM_GROUP_POSE_VELOCITY;
		} else {
			groups[i] = MM_GROUP_EXTRA;
		}
	}

	emit_changed();
}

void MMFeatureSchema::set_trajectory_times(const PackedFloat32Array &p_times) {
	_trajectory_times = p_times;
	_rebuild_layout();
}

void MMFeatureSchema::set_pose_bones(const PackedStringArray &p_bones) {
	_pose_bones = p_bones;
	_rebuild_layout();
}

void MMFeatureSchema::set_root_bone(const String &p_bone) {
	_root_bone = p_bone;
	emit_changed();
}

void MMFeatureSchema::set_include_bone_velocity(bool p_enabled) {
	_include_bone_velocity = p_enabled;
	_rebuild_layout();
}

void MMFeatureSchema::set_include_root_velocity(bool p_enabled) {
	_include_root_velocity = p_enabled;
	_rebuild_layout();
}

void MMFeatureSchema::set_include_yaw_rate(bool p_enabled) {
	_include_yaw_rate = p_enabled;
	_rebuild_layout();
}

void MMFeatureSchema::set_extra_dimensions(int p_count) {
	_extra_dimensions = p_count < 0 ? 0 : p_count;
	_rebuild_layout();
}

int MMFeatureSchema::get_group_of_dimension(int p_dimension) const {
	if (p_dimension < 0 || p_dimension >= _dimension_groups.size()) {
		return MM_GROUP_EXTRA;
	}
	return _dimension_groups[p_dimension];
}

const uint8_t *MMFeatureSchema::get_group_table() const {
	return _dimension_groups.ptr();
}

PackedFloat32Array MMFeatureSchema::make_vector() const {
	PackedFloat32Array vector;
	vector.resize(_dimension);
	vector.fill(0.0f);
	return vector;
}

int MMFeatureSchema::get_lod_dimension(int p_quality) const {
	switch (p_quality) {
		case MM_QUALITY_LOW:
			return _offset_bone_position;
		case MM_QUALITY_MEDIUM:
			return _offset_bone_velocity;
		case MM_QUALITY_HIGH:
			return _offset_extra;
		default:
			return _dimension;
	}
}

void MMFeatureSchema::set_skeleton_profile(const Ref<MMSkeletonProfile> &p_profile) {
	_skeleton_profile = p_profile;
	emit_changed();
}

// Resolution order: anything the user typed is kept, anything empty is filled
// from the skeleton profile. This is the only path by which bone names enter the
// framework.
bool MMFeatureSchema::apply_skeleton_profile(Skeleton3D *p_skeleton) {
	ERR_FAIL_NULL_V(p_skeleton, false);

	if (_skeleton_profile.is_null()) {
		_skeleton_profile.instantiate();
	}
	if (!_skeleton_profile->auto_detect(p_skeleton)) {
		return false;
	}

	if (_root_bone.is_empty()) {
		_root_bone = _skeleton_profile->get_bone_name(MM_BONE_ROOT);
	}

	if (_pose_bones.is_empty()) {
		_pose_bones = _skeleton_profile->get_default_pose_bones();
		if (_include_hands) {
			PackedStringArray hand_bones = _skeleton_profile->get_hand_bones();
			for (int i = 0; i < hand_bones.size(); i++) {
				_pose_bones.push_back(hand_bones[i]);
			}
		}
	}

	// Drop anything the skeleton does not actually have, so a schema shared
	// between two different rigs degrades instead of breaking.
	PackedStringArray valid;
	for (int i = 0; i < _pose_bones.size(); i++) {
		if (p_skeleton->find_bone(_pose_bones[i]) >= 0) {
			valid.push_back(_pose_bones[i]);
		}
	}
	_pose_bones = valid;

	_rebuild_layout();
	return !_pose_bones.is_empty();
}

Ref<MMFeatureSchema> MMFeatureSchema::make_default() {
	Ref<MMFeatureSchema> schema;
	schema.instantiate();
	return schema;
}

void MMFeatureSchema::_bind_methods() {
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::PACKED_FLOAT32_ARRAY, trajectory_times)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::PACKED_STRING_ARRAY, pose_bones)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::STRING, root_bone)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::BOOL, include_bone_velocity)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::BOOL, include_root_velocity)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::BOOL, include_yaw_rate)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::INT, extra_dimensions)
	MM_BIND_PROPERTY(MMFeatureSchema, Variant::BOOL, include_hands)

	ClassDB::bind_method(D_METHOD("set_skeleton_profile", "profile"), &MMFeatureSchema::set_skeleton_profile);
	ClassDB::bind_method(D_METHOD("get_skeleton_profile"), &MMFeatureSchema::get_skeleton_profile);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "skeleton_profile", PROPERTY_HINT_RESOURCE_TYPE, "MMSkeletonProfile"),
			"set_skeleton_profile", "get_skeleton_profile");

	ClassDB::bind_method(D_METHOD("apply_skeleton_profile", "skeleton"), &MMFeatureSchema::apply_skeleton_profile);

	ClassDB::bind_method(D_METHOD("get_dimension"), &MMFeatureSchema::get_dimension);
	ClassDB::bind_method(D_METHOD("get_trajectory_point_count"), &MMFeatureSchema::get_trajectory_point_count);
	ClassDB::bind_method(D_METHOD("get_pose_bone_count"), &MMFeatureSchema::get_pose_bone_count);
	ClassDB::bind_method(D_METHOD("get_trajectory_position_offset"), &MMFeatureSchema::get_trajectory_position_offset);
	ClassDB::bind_method(D_METHOD("get_trajectory_direction_offset"), &MMFeatureSchema::get_trajectory_direction_offset);
	ClassDB::bind_method(D_METHOD("get_yaw_rate_offset"), &MMFeatureSchema::get_yaw_rate_offset);
	ClassDB::bind_method(D_METHOD("get_bone_position_offset"), &MMFeatureSchema::get_bone_position_offset);
	ClassDB::bind_method(D_METHOD("get_bone_velocity_offset"), &MMFeatureSchema::get_bone_velocity_offset);
	ClassDB::bind_method(D_METHOD("get_root_velocity_offset"), &MMFeatureSchema::get_root_velocity_offset);
	ClassDB::bind_method(D_METHOD("get_extra_offset"), &MMFeatureSchema::get_extra_offset);
	ClassDB::bind_method(D_METHOD("get_group_of_dimension", "dimension"), &MMFeatureSchema::get_group_of_dimension);
	ClassDB::bind_method(D_METHOD("get_lod_dimension", "quality"), &MMFeatureSchema::get_lod_dimension);
	ClassDB::bind_method(D_METHOD("make_vector"), &MMFeatureSchema::make_vector);

	BIND_ENUM_CONSTANT(MM_GROUP_TRAJECTORY_POSITION);
	BIND_ENUM_CONSTANT(MM_GROUP_TRAJECTORY_DIRECTION);
	BIND_ENUM_CONSTANT(MM_GROUP_POSE_POSITION);
	BIND_ENUM_CONSTANT(MM_GROUP_POSE_VELOCITY);
	BIND_ENUM_CONSTANT(MM_GROUP_ROOT_VELOCITY);
	BIND_ENUM_CONSTANT(MM_GROUP_EXTRA);
	BIND_ENUM_CONSTANT(MM_GROUP_YAW_RATE);

	BIND_ENUM_CONSTANT(MM_QUALITY_ULTRA);
	BIND_ENUM_CONSTANT(MM_QUALITY_HIGH);
	BIND_ENUM_CONSTANT(MM_QUALITY_MEDIUM);
	BIND_ENUM_CONSTANT(MM_QUALITY_LOW);
}
