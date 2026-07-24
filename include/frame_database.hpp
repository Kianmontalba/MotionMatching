#ifndef MM_FRAME_DATABASE_HPP
#define MM_FRAME_DATABASE_HPP

#include "feature.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMAnimationEntry
//
// One source clip inside the database. Frames reference their entry by index,
// so per clip metadata is stored exactly once.
// ---------------------------------------------------------------------------
class MMAnimationEntry : public Resource {
	GDCLASS(MMAnimationEntry, Resource);

private:
	String _animation_name;
	String _library_name;
	int _category = MM_CATEGORY_LOCOMOTION;
	int _tags = 0;
	float _length = 0.0f;
	bool _loop = false;
	bool _mirrored = false;
	int _frame_start = 0;
	int _frame_count = 0;
	float _speed_min = 0.0f;
	float _speed_max = 0.0f;

protected:
	static void _bind_methods();

public:
	MM_ACCESSORS(String, animation_name)
	MM_ACCESSORS(String, library_name)
	MM_ACCESSORS(int, category)
	MM_ACCESSORS(int, tags)
	MM_ACCESSORS(float, length)
	MM_ACCESSORS(bool, loop)
	MM_ACCESSORS(bool, mirrored)
	MM_ACCESSORS(int, frame_start)
	MM_ACCESSORS(int, frame_count)
	MM_ACCESSORS(float, speed_min)
	MM_ACCESSORS(float, speed_max)

	// Full name as the AnimationMixer expects it ("library/clip", or just
	// "clip" when the library name is empty).
	StringName get_qualified_name() const;
};

// ---------------------------------------------------------------------------
// MotionMatchingDatabase
//
// The searchable motion database. Feature vectors live in one flat, cache
// friendly float array; every other per frame value lives in its own packed
// array so a filter pass only touches the memory it needs.
//
// Features are stored already normalized (mean removed, divided by the group
// scale) so a runtime search never has to normalize a million frames. Queries
// are normalized on the way in with normalize_query().
// ---------------------------------------------------------------------------
class MotionMatchingDatabase : public Resource {
	GDCLASS(MotionMatchingDatabase, Resource);

private:
	Ref<MMFeatureSchema> _schema;
	TypedArray<MMAnimationEntry> _animations;

	PackedFloat32Array _features; // frame_count * dimension, normalized.
	PackedFloat32Array _feature_mean; // dimension
	PackedFloat32Array _feature_scale; // dimension, one value per group

	PackedInt32Array _frame_animation;
	PackedFloat32Array _frame_time;
	PackedFloat32Array _frame_normalized_time;
	PackedVector3Array _frame_root_velocity;
	PackedFloat32Array _frame_angular_velocity;
	PackedFloat32Array _frame_speed;
	PackedInt32Array _frame_tags;
	PackedByteArray _frame_category;
	PackedByteArray _frame_contacts; // bit 0 = left foot, bit 1 = right foot

	float _sample_rate = 30.0f;
	int _dimension = 0;

	// 0 means "never finalized" or "saved before this field existed" — either
	// way, distinguishable from a real version number, which starts at 1.
	int _format_version = 0;

protected:
	static void _bind_methods();

public:
	void set_schema(const Ref<MMFeatureSchema> &p_schema);
	Ref<MMFeatureSchema> get_schema() const { return _schema; }

	void set_animations(const TypedArray<MMAnimationEntry> &p_animations);
	TypedArray<MMAnimationEntry> get_animations() const { return _animations; }

	MM_ACCESSORS(float, sample_rate)

	// format_version is set automatically by finalize(); it is settable here
	// too only so a loaded resource's saved value round-trips correctly.
	MM_ACCESSORS(int, format_version)
	bool is_format_compatible() const { return _format_version == MM_DATABASE_FORMAT_VERSION; }

	// Storage accessors. Bound with PROPERTY_USAGE_STORAGE so a built database
	// survives a .res round trip without polluting the inspector.
	MM_ACCESSORS(PackedFloat32Array, features)
	MM_ACCESSORS(PackedFloat32Array, feature_mean)
	MM_ACCESSORS(PackedFloat32Array, feature_scale)
	MM_ACCESSORS(PackedInt32Array, frame_animation)
	MM_ACCESSORS(PackedFloat32Array, frame_time)
	MM_ACCESSORS(PackedFloat32Array, frame_normalized_time)
	MM_ACCESSORS(PackedVector3Array, frame_root_velocity)
	MM_ACCESSORS(PackedFloat32Array, frame_angular_velocity)
	MM_ACCESSORS(PackedFloat32Array, frame_speed)
	MM_ACCESSORS(PackedInt32Array, frame_tags)
	MM_ACCESSORS(PackedByteArray, frame_category)
	MM_ACCESSORS(PackedByteArray, frame_contacts)

	int get_frame_count() const;
	int get_dimension() const { return _dimension; }
	int get_animation_count() const { return _animations.size(); }
	Ref<MMAnimationEntry> get_animation_entry(int p_id) const;

	// Raw pointer into the normalized feature block of a frame. Hot path.
	_FORCE_INLINE_ const float *get_frame_features(int p_frame) const {
		return _features.ptr() + (int64_t)p_frame * _dimension;
	}

	_FORCE_INLINE_ uint32_t get_frame_tag_mask(int p_frame) const {
		return (uint32_t)_frame_tags[p_frame];
	}

	_FORCE_INLINE_ uint8_t get_frame_category_id(int p_frame) const {
		return _frame_category[p_frame];
	}

	_FORCE_INLINE_ float get_frame_speed_value(int p_frame) const {
		return _frame_speed[p_frame];
	}

	_FORCE_INLINE_ int get_frame_animation_id(int p_frame) const {
		return _frame_animation[p_frame];
	}

	_FORCE_INLINE_ float get_frame_time_value(int p_frame) const {
		return _frame_time[p_frame];
	}

	_FORCE_INLINE_ float get_frame_normalized_value(int p_frame) const {
		return _frame_normalized_time[p_frame];
	}

	_FORCE_INLINE_ Vector3 get_frame_root_velocity_value(int p_frame) const {
		return _frame_root_velocity[p_frame];
	}

	_FORCE_INLINE_ float get_frame_angular_value(int p_frame) const {
		return _frame_angular_velocity[p_frame];
	}

	_FORCE_INLINE_ uint8_t get_frame_contacts_value(int p_frame) const {
		return _frame_contacts[p_frame];
	}

	// Frame that naturally follows p_frame. Returns -1 at the end of a non
	// looping clip, which is what tells the controller a new search is due.
	int get_next_frame(int p_frame) const;

	// Frame nearest to a time inside a clip, used to convert playback position
	// back into a database index for continuation costs.
	int get_frame_at_time(int p_animation_id, float p_time) const;

	// Turns a raw query into the normalized space the database is stored in.
	void normalize_query(PackedFloat32Array &r_query) const;
	void normalize_query_ptr(float *r_query) const;

	// Called by the extractor once every frame has been appended.
	void finalize(int p_dimension);

	void clear();

	// Editor and debug helpers.
	Dictionary get_frame_info(int p_frame) const;
	Dictionary get_statistics() const;
	PackedInt32Array find_frames_by_tags(int p_required, int p_blocked) const;
};

} // namespace godot

#endif // MM_FRAME_DATABASE_HPP
