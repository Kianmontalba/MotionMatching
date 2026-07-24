#ifndef MM_FEATURE_HPP
#define MM_FEATURE_HPP

#include "mm_types.hpp"
#include "skeleton_profile.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMFeatureSchema
//
// Describes what a feature vector contains and where each value lives inside
// it. The database, the extractor, the cost function and the runtime query all
// read their layout from this single resource, so adding a feature never
// requires touching the search code.
//
// Layout (all values are in character space, -Z forward):
//
//   [ trajectory positions  ] 2 floats (x, z) per trajectory point
//   [ trajectory directions ] 2 floats (x, z) per trajectory point
//   [ root velocity         ] 3 floats                    (optional)
//   [ bone positions        ] 3 floats per tracked bone
//   [ bone velocities       ] 3 floats per tracked bone   (optional)
//   [ extra                 ] user defined dimensions     (optional)
//
// The order is deliberate: every quality level is a prefix of the full vector,
// so a low LOD search is the same loop with a smaller bound.
// ---------------------------------------------------------------------------
class MMFeatureSchema : public Resource {
	GDCLASS(MMFeatureSchema, Resource);

private:
	PackedFloat32Array _trajectory_times;
	PackedStringArray _pose_bones;
	String _root_bone;
	Ref<MMSkeletonProfile> _skeleton_profile;
	bool _include_hands = false;
	bool _include_bone_velocity = true;
	bool _include_root_velocity = true;
	int _extra_dimensions = 0;

	// Cached layout, rebuilt whenever a setting changes.
	int _dimension = 0;
	int _offset_trajectory_position = 0;
	int _offset_trajectory_direction = 0;
	int _offset_bone_position = 0;
	int _offset_bone_velocity = 0;
	int _offset_root_velocity = 0;
	int _offset_extra = 0;
	PackedByteArray _dimension_groups;

	void _rebuild_layout();

protected:
	static void _bind_methods();

public:
	MMFeatureSchema();

	void set_trajectory_times(const PackedFloat32Array &p_times);
	PackedFloat32Array get_trajectory_times() const { return _trajectory_times; }

	void set_pose_bones(const PackedStringArray &p_bones);
	PackedStringArray get_pose_bones() const { return _pose_bones; }

	void set_root_bone(const String &p_bone);
	String get_root_bone() const { return _root_bone; }

	void set_skeleton_profile(const Ref<MMSkeletonProfile> &p_profile);
	Ref<MMSkeletonProfile> get_skeleton_profile() const { return _skeleton_profile; }

	MM_ACCESSORS(bool, include_hands)

	// Runs skeleton detection and fills the pose bone list from the detected
	// roles. Called by the extractor before a build, and safe to call from a
	// tool script. Bone names are never assumed anywhere else in the
	// framework; this is the only place they enter it.
	bool apply_skeleton_profile(Skeleton3D *p_skeleton);

	void set_include_bone_velocity(bool p_enabled);
	bool get_include_bone_velocity() const { return _include_bone_velocity; }

	void set_include_root_velocity(bool p_enabled);
	bool get_include_root_velocity() const { return _include_root_velocity; }

	void set_extra_dimensions(int p_count);
	int get_extra_dimensions() const { return _extra_dimensions; }

	// Layout queries.
	int get_dimension() const { return _dimension; }
	int get_trajectory_point_count() const { return _trajectory_times.size(); }
	int get_pose_bone_count() const { return _pose_bones.size(); }

	int get_trajectory_position_offset() const { return _offset_trajectory_position; }
	int get_trajectory_direction_offset() const { return _offset_trajectory_direction; }
	int get_bone_position_offset() const { return _offset_bone_position; }
	int get_bone_velocity_offset() const { return _offset_bone_velocity; }
	int get_root_velocity_offset() const { return _offset_root_velocity; }
	int get_extra_offset() const { return _offset_extra; }

	// Group of a single dimension, used for normalization and weighting.
	int get_group_of_dimension(int p_dimension) const;
	const uint8_t *get_group_table() const;

	// Allocates a zeroed vector matching this schema.
	PackedFloat32Array make_vector() const;

	// Reduces the schema to a cheaper subset for LOD searches. Returns the
	// number of dimensions that should be compared for the given quality.
	int get_lod_dimension(int p_quality) const;

	static Ref<MMFeatureSchema> make_default();
};

} // namespace godot

#endif // MM_FEATURE_HPP
