#include "frame_database.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// MMAnimationEntry
// ---------------------------------------------------------------------------

StringName MMAnimationEntry::get_qualified_name() const {
	if (_library_name.is_empty()) {
		return StringName(_animation_name);
	}
	return StringName(_library_name + String("/") + _animation_name);
}

void MMAnimationEntry::_bind_methods() {
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::STRING, animation_name)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::STRING, library_name)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::INT, category)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::INT, tags)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::FLOAT, length)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::BOOL, loop)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::BOOL, mirrored)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::INT, frame_start)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::INT, frame_count)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::FLOAT, speed_min)
	MM_BIND_PROPERTY(MMAnimationEntry, Variant::FLOAT, speed_max)

	ClassDB::bind_method(D_METHOD("get_qualified_name"), &MMAnimationEntry::get_qualified_name);
}

// ---------------------------------------------------------------------------
// MotionMatchingDatabase
// ---------------------------------------------------------------------------

void MotionMatchingDatabase::set_schema(const Ref<MMFeatureSchema> &p_schema) {
	_schema = p_schema;
	if (_schema.is_valid()) {
		_dimension = _schema->get_dimension();
	}
	emit_changed();
}

void MotionMatchingDatabase::set_animations(const TypedArray<MMAnimationEntry> &p_animations) {
	_animations = p_animations;
	emit_changed();
}

void MotionMatchingDatabase::set_all_loop(bool p_loop) {
	for (int i = 0; i < _animations.size(); i++) {
		Ref<MMAnimationEntry> entry = _animations[i];
		if (entry.is_valid()) {
			entry->set_loop(p_loop);
		}
	}
	emit_changed();
}

int MotionMatchingDatabase::get_frame_count() const {
	return _frame_animation.size();
}

Ref<MMAnimationEntry> MotionMatchingDatabase::get_animation_entry(int p_id) const {
	if (p_id < 0 || p_id >= _animations.size()) {
		return Ref<MMAnimationEntry>();
	}
	return _animations[p_id];
}

// The frame that plays next if nothing changes. Returning -1 at the tail of a
// non looping clip is how the controller learns that a new search is required
// rather than letting the character freeze on the last pose.
int MotionMatchingDatabase::get_next_frame(int p_frame) const {
	if (p_frame < 0 || p_frame >= get_frame_count()) {
		return -1;
	}
	const int animation_id = _frame_animation[p_frame];
	Ref<MMAnimationEntry> entry = get_animation_entry(animation_id);
	if (entry.is_null()) {
		return -1;
	}

	const int last = entry->get_frame_start() + entry->get_frame_count() - 1;
	if (p_frame < last) {
		return p_frame + 1;
	}
	return entry->get_loop() ? entry->get_frame_start() : -1;
}

int MotionMatchingDatabase::get_frame_at_time(int p_animation_id, float p_time) const {
	Ref<MMAnimationEntry> entry = get_animation_entry(p_animation_id);
	if (entry.is_null() || entry->get_frame_count() <= 0) {
		return -1;
	}

	const float length = entry->get_length();
	float time = p_time;
	if (entry->get_loop() && length > 0.0f) {
		time = Math::fposmod(time, length);
	}

	int offset = (int)Math::round(time * _sample_rate);
	offset = CLAMP(offset, 0, entry->get_frame_count() - 1);
	return entry->get_frame_start() + offset;
}

PackedFloat32Array MotionMatchingDatabase::normalize_query(PackedFloat32Array p_query) const {
	if (p_query.size() < _dimension) {
		p_query.resize(_dimension);
	}
	normalize_query_ptr(p_query.ptrw());
	return p_query;
}

void MotionMatchingDatabase::normalize_query_ptr(float *r_query) const {
	const float *mean = _feature_mean.ptr();
	const float *scale = _feature_scale.ptr();
	if (mean == nullptr || scale == nullptr) {
		return;
	}
	for (int i = 0; i < _dimension; i++) {
		r_query[i] = (r_query[i] - mean[i]) / scale[i];
	}
}

// Normalization strategy: the mean is removed per dimension, but the scale is
// shared across every dimension of a feature group. Sharing the scale keeps
// the internal geometry of a group intact, so a foot that is 10 cm off still
// costs more than a foot that is 2 cm off, while a group measured in metres
// per second no longer drowns out a group measured in metres.
void MotionMatchingDatabase::finalize(int p_dimension) {
	_dimension = p_dimension;
	const int frames = get_frame_count();
	ERR_FAIL_COND_MSG(frames <= 0, "Cannot finalize an empty motion database.");
	ERR_FAIL_COND_MSG(_features.size() != (int64_t)frames * _dimension,
			"Feature block size does not match the frame count.");

	_format_version = MM_DATABASE_FORMAT_VERSION;

	_feature_mean.resize(_dimension);
	_feature_scale.resize(_dimension);
	float *mean = _feature_mean.ptrw();
	float *scale = _feature_scale.ptrw();
	float *features = _features.ptrw();

	for (int d = 0; d < _dimension; d++) {
		double sum = 0.0;
		for (int f = 0; f < frames; f++) {
			sum += features[(int64_t)f * _dimension + d];
		}
		mean[d] = (float)(sum / (double)frames);
	}

	// Variance per dimension, then averaged inside each group.
	double group_variance[MM_GROUP_MAX] = { 0.0 };
	int group_size[MM_GROUP_MAX] = { 0 };

	for (int d = 0; d < _dimension; d++) {
		double sum = 0.0;
		for (int f = 0; f < frames; f++) {
			const double delta = features[(int64_t)f * _dimension + d] - mean[d];
			sum += delta * delta;
		}
		const int group = _schema.is_valid() ? _schema->get_group_of_dimension(d) : MM_GROUP_EXTRA;
		group_variance[group] += sum / (double)frames;
		group_size[group]++;
	}

	for (int d = 0; d < _dimension; d++) {
		const int group = _schema.is_valid() ? _schema->get_group_of_dimension(d) : MM_GROUP_EXTRA;
		const int size = group_size[group] > 0 ? group_size[group] : 1;
		const double variance = group_variance[group] / (double)size;
		float deviation = (float)Math::sqrt(variance);
		if (deviation < 0.0001f) {
			deviation = 1.0f; // A constant feature must not explode the cost.
		}
		scale[d] = deviation;
	}

	for (int f = 0; f < frames; f++) {
		float *row = features + (int64_t)f * _dimension;
		for (int d = 0; d < _dimension; d++) {
			row[d] = (row[d] - mean[d]) / scale[d];
		}
	}

	emit_changed();
}

// Reduces resident memory by keeping only every p_stride-th frame per clip.
// Frames belong to exactly one clip (frame_start/frame_count), so the
// reduction runs per clip rather than across the whole flat array -- this is
// what keeps a clip boundary from ever blending frames that were never
// adjacent in the source animation. The clip's true last frame is always
// kept in addition to the strided picks, since that is the exact pose a
// non-looping clip (a landing, a stop) blends out to; losing it would make
// the blend-out reach for a slightly earlier pose instead.
//
// _sample_rate is divided by the same factor afterwards: get_frame_at_time()
// converts a playback time into a frame offset by multiplying by
// _sample_rate, and every kept frame is now p_stride times further apart in
// time than before, so leaving the rate unchanged would make every time
// lookup land roughly p_stride frames too far into the clip.
void MotionMatchingDatabase::reduce_frame_stride(int p_stride) {
	if (p_stride <= 1 || p_stride <= _applied_stride || _dimension <= 0) {
		return;
	}

	// How much further to thin an already-reduced copy: e.g. going from
	// applied=2 to a requested 6 means dropping to every 3rd of what is
	// already kept, not starting over from data that no longer exists.
	const int relative_stride = p_stride / _applied_stride;
	if (relative_stride <= 1) {
		return;
	}

	const float *old_features = _features.ptr();

	PackedFloat32Array new_features;
	PackedInt32Array new_frame_animation;
	PackedFloat32Array new_frame_time;
	PackedFloat32Array new_frame_normalized_time;
	PackedVector3Array new_frame_root_velocity;
	PackedFloat32Array new_frame_angular_velocity;
	PackedFloat32Array new_frame_speed;
	PackedInt32Array new_frame_tags;
	PackedByteArray new_frame_category;
	PackedByteArray new_frame_contacts;

	int new_count = 0;

	for (int a = 0; a < _animations.size(); a++) {
		Ref<MMAnimationEntry> entry = _animations[a];
		if (entry.is_null()) {
			continue;
		}

		const int start = entry->get_frame_start();
		const int count = entry->get_frame_count();
		if (count <= 0) {
			entry->set_frame_start(new_count);
			entry->set_frame_count(0);
			continue;
		}

		const int new_start = new_count;
		int kept_in_clip = 0;
		int last_kept_src = -1;

		for (int f = 0; f < count; f += relative_stride) {
			const int src = start + f;
			for (int d = 0; d < _dimension; d++) {
				new_features.push_back(old_features[(int64_t)src * _dimension + d]);
			}
			new_frame_animation.push_back(a);
			new_frame_time.push_back(_frame_time[src]);
			new_frame_normalized_time.push_back(_frame_normalized_time[src]);
			new_frame_root_velocity.push_back(_frame_root_velocity[src]);
			new_frame_angular_velocity.push_back(_frame_angular_velocity[src]);
			new_frame_speed.push_back(_frame_speed[src]);
			new_frame_tags.push_back(_frame_tags[src]);
			new_frame_category.push_back(_frame_category[src]);
			new_frame_contacts.push_back(_frame_contacts[src]);
			last_kept_src = src;
			kept_in_clip++;
			new_count++;
		}

		const int true_last_src = start + count - 1;
		if (last_kept_src != true_last_src) {
			for (int d = 0; d < _dimension; d++) {
				new_features.push_back(old_features[(int64_t)true_last_src * _dimension + d]);
			}
			new_frame_animation.push_back(a);
			new_frame_time.push_back(_frame_time[true_last_src]);
			new_frame_normalized_time.push_back(_frame_normalized_time[true_last_src]);
			new_frame_root_velocity.push_back(_frame_root_velocity[true_last_src]);
			new_frame_angular_velocity.push_back(_frame_angular_velocity[true_last_src]);
			new_frame_speed.push_back(_frame_speed[true_last_src]);
			new_frame_tags.push_back(_frame_tags[true_last_src]);
			new_frame_category.push_back(_frame_category[true_last_src]);
			new_frame_contacts.push_back(_frame_contacts[true_last_src]);
			kept_in_clip++;
			new_count++;
		}

		entry->set_frame_start(new_start);
		entry->set_frame_count(kept_in_clip);
	}

	_features = new_features;
	_frame_animation = new_frame_animation;
	_frame_time = new_frame_time;
	_frame_normalized_time = new_frame_normalized_time;
	_frame_root_velocity = new_frame_root_velocity;
	_frame_angular_velocity = new_frame_angular_velocity;
	_frame_speed = new_frame_speed;
	_frame_tags = new_frame_tags;
	_frame_category = new_frame_category;
	_frame_contacts = new_frame_contacts;

	_sample_rate /= (float)relative_stride;
	_applied_stride = p_stride;

	emit_changed();
}

void MotionMatchingDatabase::clear() {
	_animations.clear();
	_features.clear();
	_feature_mean.clear();
	_feature_scale.clear();
	_frame_animation.clear();
	_frame_time.clear();
	_frame_normalized_time.clear();
	_frame_root_velocity.clear();
	_frame_angular_velocity.clear();
	_frame_speed.clear();
	_frame_tags.clear();
	_frame_category.clear();
	_frame_contacts.clear();
	_applied_stride = 1;
	emit_changed();
}

Dictionary MotionMatchingDatabase::get_frame_info(int p_frame) const {
	Dictionary info;
	if (p_frame < 0 || p_frame >= get_frame_count()) {
		return info;
	}

	Ref<MMAnimationEntry> entry = get_animation_entry(_frame_animation[p_frame]);
	info["frame_index"] = p_frame;
	info["animation_id"] = _frame_animation[p_frame];
	info["animation_name"] = entry.is_valid() ? entry->get_animation_name() : String();
	info["clip_name"] = entry.is_valid() ? String(entry->get_qualified_name()) : String();
	info["time"] = _frame_time[p_frame];
	info["normalized_time"] = _frame_normalized_time[p_frame];
	info["root_velocity"] = _frame_root_velocity[p_frame];
	info["angular_velocity"] = _frame_angular_velocity[p_frame];
	info["speed"] = _frame_speed[p_frame];
	info["tags"] = _frame_tags[p_frame];
	info["category"] = (int)_frame_category[p_frame];
	info["left_foot_contact"] = (_frame_contacts[p_frame] & 1) != 0;
	info["right_foot_contact"] = (_frame_contacts[p_frame] & 2) != 0;
	return info;
}

Dictionary MotionMatchingDatabase::get_statistics() const {
	Dictionary stats;
	const int frames = get_frame_count();
	stats["frame_count"] = frames;
	stats["animation_count"] = _animations.size();
	stats["dimension"] = _dimension;
	stats["sample_rate"] = _sample_rate;
	stats["feature_bytes"] = (int64_t)_features.size() * 4;
	stats["metadata_bytes"] = (int64_t)frames * (4 + 4 + 4 + 12 + 4 + 4 + 4 + 1 + 1);

	int per_category[MM_CATEGORY_MAX] = { 0 };
	for (int i = 0; i < frames; i++) {
		const uint8_t category = _frame_category[i];
		if (category < MM_CATEGORY_MAX) {
			per_category[category]++;
		}
	}
	Array categories;
	for (int i = 0; i < MM_CATEGORY_MAX; i++) {
		categories.push_back(per_category[i]);
	}
	stats["frames_per_category"] = categories;

	double total_seconds = 0.0;
	for (int i = 0; i < _animations.size(); i++) {
		Ref<MMAnimationEntry> entry = _animations[i];
		if (entry.is_valid()) {
			total_seconds += entry->get_length();
		}
	}
	stats["total_seconds"] = total_seconds;
	return stats;
}

PackedInt32Array MotionMatchingDatabase::find_frames_by_tags(int p_required, int p_blocked) const {
	PackedInt32Array result;
	const int frames = get_frame_count();
	for (int i = 0; i < frames; i++) {
		const uint32_t tags = (uint32_t)_frame_tags[i];
		if ((tags & (uint32_t)p_required) != (uint32_t)p_required) {
			continue;
		}
		if (tags & (uint32_t)p_blocked) {
			continue;
		}
		result.push_back(i);
	}
	return result;
}

void MotionMatchingDatabase::_bind_methods() {
	MM_BIND_PROPERTY(MotionMatchingDatabase, Variant::STRING, name)

	ClassDB::bind_method(D_METHOD("set_schema", "schema"), &MotionMatchingDatabase::set_schema);
	ClassDB::bind_method(D_METHOD("get_schema"), &MotionMatchingDatabase::get_schema);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "schema", PROPERTY_HINT_RESOURCE_TYPE, "MMFeatureSchema"),
			"set_schema", "get_schema");

	ClassDB::bind_method(D_METHOD("set_animations", "animations"), &MotionMatchingDatabase::set_animations);
	ClassDB::bind_method(D_METHOD("get_animations"), &MotionMatchingDatabase::get_animations);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "animations", PROPERTY_HINT_ARRAY_TYPE,
						  "24/17:MMAnimationEntry"),
			"set_animations", "get_animations");

	ClassDB::bind_method(D_METHOD("set_all_loop", "loop"), &MotionMatchingDatabase::set_all_loop);

	MM_BIND_PROPERTY(MotionMatchingDatabase, Variant::FLOAT, sample_rate)
	MM_BIND_PROPERTY(MotionMatchingDatabase, Variant::INT, format_version)
	ClassDB::bind_method(D_METHOD("is_format_compatible"), &MotionMatchingDatabase::is_format_compatible);

	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, features)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, feature_mean)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, feature_scale)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_INT32_ARRAY, frame_animation)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, frame_time)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, frame_normalized_time)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_VECTOR3_ARRAY, frame_root_velocity)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, frame_angular_velocity)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_FLOAT32_ARRAY, frame_speed)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_INT32_ARRAY, frame_tags)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_BYTE_ARRAY, frame_category)
	MM_BIND_STORAGE(MotionMatchingDatabase, Variant::PACKED_BYTE_ARRAY, frame_contacts)

	ClassDB::bind_method(D_METHOD("get_frame_count"), &MotionMatchingDatabase::get_frame_count);
	ClassDB::bind_method(D_METHOD("get_dimension"), &MotionMatchingDatabase::get_dimension);
	ClassDB::bind_method(D_METHOD("get_animation_count"), &MotionMatchingDatabase::get_animation_count);
	ClassDB::bind_method(D_METHOD("get_animation_entry", "id"), &MotionMatchingDatabase::get_animation_entry);
	ClassDB::bind_method(D_METHOD("get_next_frame", "frame"), &MotionMatchingDatabase::get_next_frame);
	ClassDB::bind_method(D_METHOD("get_frame_at_time", "animation_id", "time"), &MotionMatchingDatabase::get_frame_at_time);
	ClassDB::bind_method(D_METHOD("normalize_query", "query"), &MotionMatchingDatabase::normalize_query);
	ClassDB::bind_method(D_METHOD("finalize", "dimension"), &MotionMatchingDatabase::finalize);
	ClassDB::bind_method(D_METHOD("reduce_frame_stride", "stride"), &MotionMatchingDatabase::reduce_frame_stride);
	ClassDB::bind_method(D_METHOD("clear"), &MotionMatchingDatabase::clear);
	ClassDB::bind_method(D_METHOD("get_frame_info", "frame"), &MotionMatchingDatabase::get_frame_info);
	ClassDB::bind_method(D_METHOD("get_statistics"), &MotionMatchingDatabase::get_statistics);
	ClassDB::bind_method(D_METHOD("find_frames_by_tags", "required", "blocked"), &MotionMatchingDatabase::find_frames_by_tags);

	BIND_ENUM_CONSTANT(MM_CATEGORY_LOCOMOTION);
	BIND_ENUM_CONSTANT(MM_CATEGORY_AIRBORNE);
	BIND_ENUM_CONSTANT(MM_CATEGORY_TRAVERSAL);
	BIND_ENUM_CONSTANT(MM_CATEGORY_COMBAT);
	BIND_ENUM_CONSTANT(MM_CATEGORY_INTERACTION);
	BIND_ENUM_CONSTANT(MM_CATEGORY_CUSTOM);

	BIND_ENUM_CONSTANT(MM_TAG_IDLE);
	BIND_ENUM_CONSTANT(MM_TAG_WALK);
	BIND_ENUM_CONSTANT(MM_TAG_JOG);
	BIND_ENUM_CONSTANT(MM_TAG_RUN);
	BIND_ENUM_CONSTANT(MM_TAG_SPRINT);
	BIND_ENUM_CONSTANT(MM_TAG_CROUCH);
	BIND_ENUM_CONSTANT(MM_TAG_PRONE);
	BIND_ENUM_CONSTANT(MM_TAG_STRAFE);
	BIND_ENUM_CONSTANT(MM_TAG_TURN);
	BIND_ENUM_CONSTANT(MM_TAG_PIVOT);
	BIND_ENUM_CONSTANT(MM_TAG_START);
	BIND_ENUM_CONSTANT(MM_TAG_STOP);
	BIND_ENUM_CONSTANT(MM_TAG_JUMP);
	BIND_ENUM_CONSTANT(MM_TAG_FALL);
	BIND_ENUM_CONSTANT(MM_TAG_LAND);
	BIND_ENUM_CONSTANT(MM_TAG_VAULT);
	BIND_ENUM_CONSTANT(MM_TAG_MANTLE);
	BIND_ENUM_CONSTANT(MM_TAG_CLIMB);
	BIND_ENUM_CONSTANT(MM_TAG_SLIDE);
	BIND_ENUM_CONSTANT(MM_TAG_ROLL);
	BIND_ENUM_CONSTANT(MM_TAG_ATTACK);
	BIND_ENUM_CONSTANT(MM_TAG_RELOAD);
	BIND_ENUM_CONSTANT(MM_TAG_HIT);
	BIND_ENUM_CONSTANT(MM_TAG_AIM);
	BIND_ENUM_CONSTANT(MM_TAG_UNARMED);
	BIND_ENUM_CONSTANT(MM_TAG_RIFLE);
	BIND_ENUM_CONSTANT(MM_TAG_PISTOL);
	BIND_ENUM_CONSTANT(MM_TAG_MIRRORED);
}
