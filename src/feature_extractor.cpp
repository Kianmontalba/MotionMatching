#include "feature_extractor.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// Projects a transform onto the ground plane, keeping only its yaw. Used when
// a clip has no dedicated root track and the hips have to stand in for one.
static Transform3D mm_project_yaw(const Transform3D &p_transform) {
	Vector3 forward = -p_transform.basis.get_column(2);
	forward.y = 0.0f;
	if (forward.length_squared() < 0.000001f) {
		forward = Vector3(0, 0, -1);
	} else {
		forward.normalize();
	}
	Basis basis = Basis::looking_at(forward, Vector3(0, 1, 0));
	return Transform3D(basis, Vector3(p_transform.origin.x, 0.0f, p_transform.origin.z));
}

// ---------------------------------------------------------------------------
// MMPoseSampler
// ---------------------------------------------------------------------------

bool MMPoseSampler::bind(Skeleton3D *p_skeleton, const Ref<Animation> &p_animation,
		const String &p_root_bone, const String &p_pelvis_bone) {
	ERR_FAIL_NULL_V(p_skeleton, false);
	ERR_FAIL_COND_V(p_animation.is_null(), false);

	_skeleton = p_skeleton;
	_animation = p_animation;
	_bone_count = p_skeleton->get_bone_count();

	_tracks.resize(_bone_count);
	_parents.resize(_bone_count);
	_rest.resize(_bone_count);
	_local.resize(_bone_count);
	_model.resize(_bone_count);

	HashMap<String, int> bone_lookup;
	for (int i = 0; i < _bone_count; i++) {
		_tracks.write[i] = BoneTracks();
		_parents.write[i] = p_skeleton->get_bone_parent(i);
		_rest.write[i] = p_skeleton->get_bone_rest(i);
		bone_lookup[p_skeleton->get_bone_name(i)] = i;
	}

	// One pass over the tracks instead of one lookup per bone. A GASP clip has
	// a few hundred tracks; a library has hundreds of thousands.
	const int track_count = p_animation->get_track_count();
	for (int t = 0; t < track_count; t++) {
		const NodePath path = p_animation->track_get_path(t);
		if (path.get_subname_count() == 0) {
			continue;
		}
		const String bone_name = path.get_subname(path.get_subname_count() - 1);
		HashMap<String, int>::Iterator it = bone_lookup.find(bone_name);
		if (!it) {
			continue;
		}

		BoneTracks &tracks = _tracks.write[it->value];
		switch (p_animation->track_get_type(t)) {
			case Animation::TYPE_POSITION_3D:
				tracks.position = t;
				break;
			case Animation::TYPE_ROTATION_3D:
				tracks.rotation = t;
				break;
			case Animation::TYPE_SCALE_3D:
				tracks.scale = t;
				break;
			default:
				break;
		}
	}

	_root_bone = -1;
	if (!p_root_bone.is_empty()) {
		HashMap<String, int>::Iterator it = bone_lookup.find(p_root_bone);
		if (it && (_tracks[it->value].position >= 0 || _tracks[it->value].rotation >= 0)) {
			// A root bone with no animated track is not a root motion source.
			_root_bone = it->value;
		}
	}

	_fallback_root = -1;
	if (!p_pelvis_bone.is_empty()) {
		HashMap<String, int>::Iterator it = bone_lookup.find(p_pelvis_bone);
		if (it) {
			_fallback_root = it->value;
		}
	}

	_bound_tracks = 0;
	for (int i = 0; i < _bone_count; i++) {
		if (_tracks[i].position >= 0 || _tracks[i].rotation >= 0) {
			_bound_tracks++;
		}
	}

	return _bound_tracks > 0;
}

void MMPoseSampler::unbind() {
	_skeleton = nullptr;
	_animation.unref();
	_tracks.clear();
	_bone_count = 0;
	_root_bone = -1;
}

void MMPoseSampler::sample(double p_time) {
	if (_animation.is_null() || _bone_count == 0) {
		return;
	}

	for (int i = 0; i < _bone_count; i++) {
		const BoneTracks &tracks = _tracks[i];
		Transform3D local = _rest[i];

		if (tracks.position >= 0) {
			local.origin = _animation->position_track_interpolate(tracks.position, p_time);
		}
		if (tracks.rotation >= 0) {
			const Quaternion rotation = _animation->rotation_track_interpolate(tracks.rotation, p_time);
			const Vector3 scale = local.basis.get_scale();
			local.basis = Basis(rotation).scaled(scale);
		}
		if (tracks.scale >= 0) {
			const Vector3 scale = _animation->scale_track_interpolate(tracks.scale, p_time);
			local.basis = local.basis.orthonormalized().scaled(scale);
		}

		_local.write[i] = local;
	}

	// Bones are stored parent first in Godot, so a single forward pass gives
	// every model space transform.
	for (int i = 0; i < _bone_count; i++) {
		const int parent = _parents[i];
		if (parent >= 0) {
			_model.write[i] = _model[parent] * _local[i];
		} else {
			_model.write[i] = _local[i];
		}
	}
}

Transform3D MMPoseSampler::get_model_transform(int p_bone) const {
	if (p_bone < 0 || p_bone >= _bone_count) {
		return Transform3D();
	}
	return _model[p_bone];
}

Transform3D MMPoseSampler::get_local_transform(int p_bone) const {
	if (p_bone < 0 || p_bone >= _bone_count) {
		return Transform3D();
	}
	return _local[p_bone];
}

Transform3D MMPoseSampler::get_root_transform() const {
	if (_root_bone >= 0) {
		return _model[_root_bone];
	}
	if (_fallback_root >= 0) {
		// No animated root: the pelvis becomes the reference, flattened to the
		// ground so vertical bob does not leak into the trajectory. This is
		// the normal case for in place animation packs.
		return mm_project_yaw(_model[_fallback_root]);
	}
	if (_bone_count > 0) {
		return mm_project_yaw(_model[0]);
	}
	return Transform3D();
}

Dictionary MMPoseSampler::sample_bone(const String &p_bone_name, double p_time) {
	Dictionary result;
	if (_skeleton == nullptr) {
		return result;
	}
	const int bone = _skeleton->find_bone(p_bone_name);
	if (bone < 0) {
		return result;
	}
	sample(p_time);
	result["model"] = _model[bone];
	result["local"] = _local[bone];
	result["root"] = get_root_transform();
	return result;
}

void MMPoseSampler::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bind", "skeleton", "animation", "root_bone", "pelvis_bone"),
			&MMPoseSampler::bind);
	ClassDB::bind_method(D_METHOD("get_bound_track_count"), &MMPoseSampler::get_bound_track_count);
	ClassDB::bind_method(D_METHOD("unbind"), &MMPoseSampler::unbind);
	ClassDB::bind_method(D_METHOD("sample", "time"), &MMPoseSampler::sample);
	ClassDB::bind_method(D_METHOD("sample_bone", "bone_name", "time"), &MMPoseSampler::sample_bone);
	ClassDB::bind_method(D_METHOD("get_model_transform", "bone"), &MMPoseSampler::get_model_transform);
	ClassDB::bind_method(D_METHOD("get_local_transform", "bone"), &MMPoseSampler::get_local_transform);
	ClassDB::bind_method(D_METHOD("get_root_transform"), &MMPoseSampler::get_root_transform);
	ClassDB::bind_method(D_METHOD("get_bone_count"), &MMPoseSampler::get_bone_count);
}

// ---------------------------------------------------------------------------
// MMFeatureExtractor
// ---------------------------------------------------------------------------

void MMFeatureExtractor::set_schema(const Ref<MMFeatureSchema> &p_schema) {
	_schema = p_schema;
}

void MMFeatureExtractor::set_profile(const Ref<MMSkeletonProfile> &p_profile) {
	_profile = p_profile;
}

void MMFeatureExtractor::set_analyzer(const Ref<MMClipAnalyzer> &p_analyzer) {
	_analyzer = p_analyzer;
}

void MMFeatureExtractor::_extract_extra(float *r_features, int p_count, double p_time) {
	// Default: leave the extra block zeroed. Override to add weapon state,
	// combat phase, or any gameplay signal that should influence matching.
	for (int i = 0; i < p_count; i++) {
		r_features[i] = 0.0f;
	}
}

// Discovers everything the pipeline needs about this particular rig. Called
// once per build, and safe to call again after the skeleton changes.
bool MMFeatureExtractor::_prepare(Skeleton3D *p_skeleton) {
	ERR_FAIL_NULL_V(p_skeleton, false);

	if (_profile.is_null()) {
		_profile.instantiate();
	}
	if (_auto_detect_profile && !_profile->is_detected()) {
		if (!_profile->auto_detect(p_skeleton)) {
			ERR_PRINT("Skeleton profile detection failed: " + _profile->get_detection_report());
			return false;
		}
	}
	if (_analyzer.is_null()) {
		_analyzer.instantiate();
	}

	// The character's own scale, taken from the rest pose. Every threshold in
	// the pipeline is expressed as a ratio of this, so nothing has to be
	// retuned for a different sized character.
	_hip_height = 1.0f;
	const int pelvis = _profile->find_bone(p_skeleton, MM_BONE_PELVIS);
	if (pelvis >= 0) {
		Transform3D model;
		int walker = pelvis;
		Vector<int> chain;
		while (walker >= 0) {
			chain.insert(0, walker);
			walker = p_skeleton->get_bone_parent(walker);
		}
		for (int i = 0; i < chain.size(); i++) {
			model = model * p_skeleton->get_bone_rest(chain[i]);
		}
		if (model.origin.y > 0.05f) {
			_hip_height = model.origin.y;
		}
	}

	if (_schema.is_null()) {
		_schema.instantiate();
	}
	if (_auto_configure_schema) {
		// Seed the schema from the roles that were actually found, instead of
		// from a list of names that may not exist on this rig.
		const PackedStringArray bones = _profile->get_default_pose_bones();
		if (!bones.is_empty()) {
			_schema->set_pose_bones(bones);
		}
		_schema->set_root_bone(_profile->get_bone_name(MM_BONE_ROOT));
	}

	return true;
}

void MMFeatureExtractor::_resolve_pose_bones(Skeleton3D *p_skeleton) {
	const PackedStringArray bone_names = _schema->get_pose_bones();
	_pose_bone_indices.resize(bone_names.size());
	_foot_slots.clear();

	const String left_foot = _profile.is_valid() ? _profile->get_bone_name(MM_BONE_LEFT_FOOT) : String();
	const String right_foot = _profile.is_valid() ? _profile->get_bone_name(MM_BONE_RIGHT_FOOT) : String();

	for (int i = 0; i < bone_names.size(); i++) {
		_pose_bone_indices.write[i] = p_skeleton->find_bone(bone_names[i]);
		// Contact detection is driven by role, not by looking for the word
		// "foot" in a bone name.
		if (!left_foot.is_empty() && bone_names[i] == left_foot) {
			_foot_slots.push_back(i);
		} else if (!right_foot.is_empty() && bone_names[i] == right_foot) {
			_foot_slots.push_back(i);
		}
	}
}

// Measures the clip. This is the whole basis for automatic tagging, and it
// never looks at the clip's name.
Dictionary MMFeatureExtractor::analyze_animation(Skeleton3D *p_skeleton, const Ref<Animation> &p_animation) {
	Dictionary empty;
	ERR_FAIL_NULL_V(p_skeleton, empty);
	ERR_FAIL_COND_V(p_animation.is_null(), empty);
	if (!_prepare(p_skeleton)) {
		return empty;
	}

	if (_sampler.is_null()) {
		_sampler.instantiate();
	}
	if (!_sampler->bind(p_skeleton, p_animation, _profile->get_bone_name(MM_BONE_ROOT),
				_profile->get_bone_name(MM_BONE_PELVIS))) {
		return empty;
	}

	const int pelvis = _profile->find_bone(p_skeleton, MM_BONE_PELVIS);
	const int left_foot = _profile->find_bone(p_skeleton, MM_BONE_LEFT_FOOT);
	const int right_foot = _profile->find_bone(p_skeleton, MM_BONE_RIGHT_FOOT);

	const double length = p_animation->get_length();
	const double step = 1.0 / (double)_sample_rate;
	const int frames = MAX(2, (int)Math::floor(length / step) + 1);

	MMClipStats stats;
	stats.duration = (float)length;
	stats.hip_height = _hip_height;

	Vector3 previous_root;
	Vector3 previous_left;
	Vector3 previous_right;
	float previous_yaw = 0.0f;
	Vector3 first_position;
	Vector3 last_position;

	float speed_sum = 0.0f;
	float lateral_sum = 0.0f;
	float backward_sum = 0.0f;
	float travel_sum = 0.0f;
	int contact_frames = 0;
	int crouch_frames = 0;
	float airborne_run = 0.0f;
	float minimum_height = MM_INFINITY;
	float maximum_height = -MM_INFINITY;

	const float contact_speed = _foot_contact_speed_ratio * _hip_height;
	const float contact_height = _foot_contact_height_ratio * _hip_height;
	const float crouch_height = _hip_height * (1.0f - _analyzer->get_crouch_drop());

	for (int f = 0; f < frames; f++) {
		const double time = MIN((double)f * step, length);
		_sampler->sample(time);

		const Transform3D root = _sampler->get_root_transform();
		const Vector3 position = root.origin;
		if (f == 0) {
			first_position = position;
			previous_root = position;
			previous_left = left_foot >= 0 ? _sampler->get_model_transform(left_foot).origin : Vector3();
			previous_right = right_foot >= 0 ? _sampler->get_model_transform(right_foot).origin : Vector3();
			const Vector3 forward = -root.basis.get_column(2);
			previous_yaw = Math::atan2(forward.x, -forward.z);
		}
		last_position = position;

		const Vector3 delta = position - previous_root;
		const float travel = Vector2(delta.x, delta.z).length();
		const float speed = travel / (float)step;
		speed_sum += speed;
		stats.peak_speed = MAX(stats.peak_speed, speed / _hip_height);
		if (f == 1) {
			stats.start_speed = speed / _hip_height;
		}
		if (f == frames - 1) {
			stats.end_speed = speed / _hip_height;
		}

		// Direction of travel relative to the character's own facing, so a
		// sidestep is a sidestep regardless of world orientation.
		if (travel > 0.0001f) {
			const Vector3 local = root.basis.inverse().xform(delta);
			lateral_sum += Math::abs(local.x);
			backward_sum += MAX(local.z, 0.0f);
			travel_sum += travel;
		}

		const Vector3 forward = -root.basis.get_column(2);
		const float yaw = Math::atan2(forward.x, -forward.z);
		float yaw_delta = yaw - previous_yaw;
		while (yaw_delta > Math_PI) {
			yaw_delta -= Math_TAU;
		}
		while (yaw_delta < -Math_PI) {
			yaw_delta += Math_TAU;
		}
		stats.total_turn += Math::abs(yaw_delta);
		previous_yaw = yaw;

		// Foot contact from measured motion: a foot that is low and slow is
		// planted. No naming convention involved.
		bool planted = false;
		if (left_foot >= 0) {
			const Vector3 foot = _sampler->get_model_transform(left_foot).origin;
			const float foot_speed = (foot - previous_left).length() / (float)step;
			const float height = foot.y - position.y;
			planted = planted || (foot_speed < contact_speed && height < contact_height);
			previous_left = foot;
		}
		if (right_foot >= 0) {
			const Vector3 foot = _sampler->get_model_transform(right_foot).origin;
			const float foot_speed = (foot - previous_right).length() / (float)step;
			const float height = foot.y - position.y;
			planted = planted || (foot_speed < contact_speed && height < contact_height);
			previous_right = foot;
		}

		if (planted) {
			contact_frames++;
			airborne_run = 0.0f;
		} else {
			airborne_run += (float)step;
			stats.longest_airborne = MAX(stats.longest_airborne, airborne_run);
		}

		if (pelvis >= 0) {
			const float pelvis_height = _sampler->get_model_transform(pelvis).origin.y - position.y;
			minimum_height = MIN(minimum_height, pelvis_height);
			maximum_height = MAX(maximum_height, pelvis_height);
			if (pelvis_height < crouch_height) {
				crouch_frames++;
			}
		}

		previous_root = position;
	}

	stats.average_speed = (speed_sum / (float)frames) / _hip_height;
	stats.net_displacement = Vector2(last_position.x - first_position.x,
										last_position.z - first_position.z)
									.length() /
			_hip_height;
	stats.net_height_gain = (last_position.y - first_position.y) / _hip_height;
	stats.vertical_range = (minimum_height < maximum_height ? maximum_height - minimum_height : 0.0f) /
			_hip_height;
	stats.contact_ratio = (float)contact_frames / (float)frames;
	stats.crouch_ratio = (float)crouch_frames / (float)frames;
	stats.lateral_ratio = travel_sum > 0.0001f ? lateral_sum / travel_sum : 0.0f;
	stats.backward_ratio = travel_sum > 0.0001f ? backward_sum / travel_sum : 0.0f;

	_sampler->unbind();
	return MMClipAnalyzer::stats_to_dictionary(stats);
}

Dictionary MMFeatureExtractor::analyze_library(Skeleton3D *p_skeleton,
		const Ref<AnimationLibrary> &p_library) {
	Dictionary settings;
	ERR_FAIL_NULL_V(p_skeleton, settings);
	ERR_FAIL_COND_V(p_library.is_null(), settings);
	if (!_prepare(p_skeleton)) {
		return settings;
	}

	const PackedStringArray names = p_library->get_animation_list();
	for (int i = 0; i < names.size(); i++) {
		_progress = names.size() > 0 ? (float)i / (float)names.size() : 1.0f;
		_progress_label = names[i];

		Ref<Animation> animation = p_library->get_animation(names[i]);
		if (animation.is_null()) {
			continue;
		}

		const Dictionary stats = analyze_animation(p_skeleton, animation);
		if (stats.is_empty()) {
			continue;
		}

		const int tags = _analyzer->classify_dictionary(stats, names[i]);
		Dictionary entry;
		entry["tags"] = tags;
		entry["category"] = MMClipAnalyzer::category_for_tags(tags);
		entry["stats"] = stats;
		settings[names[i]] = entry;
	}

	_progress = 1.0f;
	return settings;
}

bool MMFeatureExtractor::append_animation(const Ref<MotionMatchingDatabase> &p_database,
		Skeleton3D *p_skeleton, const Ref<Animation> &p_animation, const String &p_name,
		const String &p_library, int p_category, int p_tags) {
	ERR_FAIL_COND_V(p_database.is_null(), false);
	ERR_FAIL_NULL_V(p_skeleton, false);
	ERR_FAIL_COND_V(p_animation.is_null(), false);
	if (!_prepare(p_skeleton)) {
		return false;
	}

	if (_sampler.is_null()) {
		_sampler.instantiate();
	}
	if (!_sampler->bind(p_skeleton, p_animation, _profile->get_bone_name(MM_BONE_ROOT),
				_profile->get_bone_name(MM_BONE_PELVIS))) {
		return false;
	}

	_resolve_pose_bones(p_skeleton);

	const PackedStringArray bone_names = _schema->get_pose_bones();
	const double length = p_animation->get_length();
	// Always true: a database entry that stops advancing when it runs out
	// of frames would freeze the character rather than search for
	// something else, and there is no per-clip reason to want that here --
	// the search itself is what decides when to leave a clip, not the
	// clip's own length. This intentionally ignores the source Animation's
	// own loop_mode (which several imported clips had left at LOOP_NONE).
	const bool loop = true;
	const double step = 1.0 / (double)_sample_rate;
	const int frame_count = MAX(1, (int)Math::floor(length / step) + 1);
	const int bone_count = bone_names.size();

	// Pass A: cache the root transform and the tracked bone positions for the
	// whole clip. Sampling is the expensive part, so it happens exactly once
	// per frame instead of once per feature.
	Vector<Transform3D> root_track;
	Vector<Vector3> bone_track;
	root_track.resize(frame_count);
	bone_track.resize(frame_count * MAX(1, bone_count));

	for (int f = 0; f < frame_count; f++) {
		const double time = MIN((double)f * step, length);
		_sampler->sample(time);
		root_track.write[f] = _sampler->get_root_transform();
		for (int b = 0; b < bone_count; b++) {
			const int bone = _pose_bone_indices[b];
			bone_track.write[f * bone_count + b] =
					bone >= 0 ? _sampler->get_model_transform(bone).origin : Vector3();
		}
	}

	// Pass B: build the feature vectors.
	const int dimension = _schema->get_dimension();
	const int trajectory_points = _schema->get_trajectory_point_count();
	const PackedFloat32Array trajectory_times = _schema->get_trajectory_times();

	PackedFloat32Array features = p_database->get_features();
	PackedInt32Array frame_animation = p_database->get_frame_animation();
	PackedFloat32Array frame_time = p_database->get_frame_time();
	PackedFloat32Array frame_normalized = p_database->get_frame_normalized_time();
	PackedVector3Array frame_root_velocity = p_database->get_frame_root_velocity();
	PackedFloat32Array frame_angular = p_database->get_frame_angular_velocity();
	PackedFloat32Array frame_speed = p_database->get_frame_speed();
	PackedInt32Array frame_tags = p_database->get_frame_tags();
	PackedByteArray frame_category = p_database->get_frame_category();
	PackedByteArray frame_contacts = p_database->get_frame_contacts();

	const int animation_id = p_database->get_animation_count();
	const int frame_start = frame_animation.size();
	const int64_t base = features.size();
	features.resize(base + (int64_t)frame_count * dimension);
	float *feature_ptr = features.ptrw() + base;

	const float contact_speed = _foot_contact_speed_ratio * _hip_height;
	const float contact_height = _foot_contact_height_ratio * _hip_height;

	float speed_min = MM_INFINITY;
	float speed_max = 0.0f;

	for (int f = 0; f < frame_count; f++) {
		float *row = feature_ptr + (int64_t)f * dimension;
		for (int d = 0; d < dimension; d++) {
			row[d] = 0.0f;
		}

		const Transform3D root = root_track[f];
		// Some rigs' root/hip bone rest orientation does not point in the
		// direction the character actually walks (commonly 180 degrees
		// off on Mixamo-style rigs). root_yaw_offset_degrees recalibrates
		// every local-space projection below -- velocity, pose, and
		// trajectory alike -- so they all agree on the same forward.
		const Basis yaw_correction = _root_yaw_offset_degrees != 0.0f
				? Basis(Vector3(0, 1, 0), Math::deg_to_rad(_root_yaw_offset_degrees))
				: Basis();
		const Basis corrected_basis = yaw_correction * root.basis;
		const Basis inverse_basis = corrected_basis.inverse();
		const Transform3D corrected_root(corrected_basis, root.origin);
		const Transform3D inverse_root = corrected_root.affine_inverse();

		const int previous = loop ? (f - 1 + frame_count) % frame_count : MAX(0, f - 1);
		const int next = loop ? (f + 1) % frame_count : MIN(frame_count - 1, f + 1);
		const double span = (double)(next - previous) * step;
		const double inverse_span = span > 0.0 ? 1.0 / span : 0.0;

		Vector3 root_velocity = (root_track[next].origin - root_track[previous].origin) * inverse_span;
		root_velocity = inverse_basis.xform(root_velocity);

		const Vector3 forward_next = -root_track[next].basis.get_column(2);
		const Vector3 forward_previous = -root_track[previous].basis.get_column(2);
		const float yaw_next = Math::atan2(forward_next.x, -forward_next.z);
		const float yaw_previous = Math::atan2(forward_previous.x, -forward_previous.z);
		float yaw_delta = yaw_next - yaw_previous;
		while (yaw_delta > Math_PI) {
			yaw_delta -= Math_TAU;
		}
		while (yaw_delta < -Math_PI) {
			yaw_delta += Math_TAU;
		}
		const float angular_velocity = (float)(yaw_delta * inverse_span);

		// Trajectory block.
		const int position_offset = _schema->get_trajectory_position_offset();
		const int direction_offset = _schema->get_trajectory_direction_offset();
		for (int t = 0; t < trajectory_points; t++) {
			const double horizon = trajectory_times[t];
			const double target_time = (double)f * step + horizon;

			Transform3D future;
			if (loop) {
				int index = (int)Math::round(target_time / step);
				index = ((index % frame_count) + frame_count) % frame_count;
				future = root_track[index];
			} else if (target_time <= length) {
				const int index = CLAMP((int)Math::round(target_time / step), 0, frame_count - 1);
				future = root_track[index];
			} else {
				// Past the end of a one shot clip: continue along the last
				// known velocity instead of flattening the line, which would
				// make every clip look like a stop.
				const double overshoot = target_time - length;
				future = root_track[frame_count - 1];
				const Vector3 tail = (root_track[frame_count - 1].origin -
											 root_track[MAX(0, frame_count - 2)].origin) /
						step;
				future.origin += tail * overshoot;
			}

			const Transform3D local = inverse_root * future;
			row[position_offset + t * 2 + 0] = local.origin.x;
			row[position_offset + t * 2 + 1] = local.origin.z;

			Vector3 direction = -local.basis.get_column(2);
			direction.y = 0.0f;
			if (direction.length_squared() > 0.000001f) {
				direction.normalize();
			} else {
				direction = Vector3(0, 0, -1);
			}
			row[direction_offset + t * 2 + 0] = direction.x;
			row[direction_offset + t * 2 + 1] = direction.z;
		}

		if (_schema->get_include_root_velocity()) {
			const int offset = _schema->get_root_velocity_offset();
			row[offset + 0] = root_velocity.x;
			row[offset + 1] = root_velocity.y;
			row[offset + 2] = root_velocity.z;
		}

		// Pose block.
		const int bone_position_offset = _schema->get_bone_position_offset();
		const int bone_velocity_offset = _schema->get_bone_velocity_offset();
		uint8_t contacts = 0;

		for (int b = 0; b < bone_count; b++) {
			const Vector3 world_position = bone_track[f * bone_count + b];
			const Vector3 local_position = inverse_root.xform(world_position);
			row[bone_position_offset + b * 3 + 0] = local_position.x;
			row[bone_position_offset + b * 3 + 1] = local_position.y;
			row[bone_position_offset + b * 3 + 2] = local_position.z;

			const Vector3 velocity = inverse_basis.xform(
					(bone_track[next * bone_count + b] - bone_track[previous * bone_count + b]) *
					inverse_span);

			if (_schema->get_include_bone_velocity()) {
				row[bone_velocity_offset + b * 3 + 0] = velocity.x;
				row[bone_velocity_offset + b * 3 + 1] = velocity.y;
				row[bone_velocity_offset + b * 3 + 2] = velocity.z;
			}

			// Contact flags come from the foot roles resolved by the profile,
			// so this works on a rig whose bones are called bone_041.
			for (int s = 0; s < _foot_slots.size(); s++) {
				if (_foot_slots[s] != b) {
					continue;
				}
				if (velocity.length() < contact_speed && local_position.y < contact_height) {
					contacts |= (uint8_t)(1 << s);
				}
			}
		}

		if (_schema->get_extra_dimensions() > 0) {
			_extract_extra(row + _schema->get_extra_offset(), _schema->get_extra_dimensions(),
					(double)f * step);
		}

		const float planar_speed = Vector2(root_velocity.x, root_velocity.z).length();
		speed_min = MIN(speed_min, planar_speed);
		speed_max = MAX(speed_max, planar_speed);

		frame_animation.push_back(animation_id);
		frame_time.push_back((float)((double)f * step));
		frame_normalized.push_back(length > 0.0 ? (float)((double)f * step / length) : 0.0f);
		frame_root_velocity.push_back(root_velocity);
		frame_angular.push_back(angular_velocity);
		frame_speed.push_back(planar_speed);
		frame_tags.push_back(p_tags);
		frame_category.push_back((uint8_t)p_category);
		frame_contacts.push_back(contacts);
	}

	p_database->set_features(features);
	p_database->set_frame_animation(frame_animation);
	p_database->set_frame_time(frame_time);
	p_database->set_frame_normalized_time(frame_normalized);
	p_database->set_frame_root_velocity(frame_root_velocity);
	p_database->set_frame_angular_velocity(frame_angular);
	p_database->set_frame_speed(frame_speed);
	p_database->set_frame_tags(frame_tags);
	p_database->set_frame_category(frame_category);
	p_database->set_frame_contacts(frame_contacts);

	Ref<MMAnimationEntry> entry;
	entry.instantiate();
	entry->set_animation_name(p_name);
	entry->set_library_name(p_library);
	entry->set_category(p_category);
	entry->set_tags(p_tags);
	entry->set_length((float)length);
	entry->set_loop(loop);
	entry->set_frame_start(frame_start);
	entry->set_frame_count(frame_count);
	entry->set_speed_min(speed_min == MM_INFINITY ? 0.0f : speed_min);
	entry->set_speed_max(speed_max);

	TypedArray<MMAnimationEntry> animations = p_database->get_animations();
	animations.push_back(entry);
	p_database->set_animations(animations);

	_sampler->unbind();
	return true;
}

Ref<MotionMatchingDatabase> MMFeatureExtractor::build_database(Skeleton3D *p_skeleton,
		const Ref<AnimationLibrary> &p_library, const Dictionary &p_clip_settings) {
	ERR_FAIL_NULL_V(p_skeleton, Ref<MotionMatchingDatabase>());
	ERR_FAIL_COND_V(p_library.is_null(), Ref<MotionMatchingDatabase>());
	if (!_prepare(p_skeleton)) {
		return Ref<MotionMatchingDatabase>();
	}

	Ref<MotionMatchingDatabase> database;
	database.instantiate();
	database->set_schema(_schema);
	database->set_sample_rate(_sample_rate);

	const PackedStringArray names = p_library->get_animation_list();
	const int total = names.size();

	for (int i = 0; i < total; i++) {
		const String name = names[i];
		_progress = total > 0 ? (float)i / (float)total : 1.0f;
		_progress_label = name;

		Ref<Animation> animation = p_library->get_animation(name);
		if (animation.is_null()) {
			continue;
		}

		int tags = 0;
		int category = MM_CATEGORY_LOCOMOTION;

		if (p_clip_settings.has(name)) {
			const Dictionary settings = p_clip_settings[name];
			tags = settings.get("tags", 0);
			category = settings.has("category") ? (int)settings["category"]
												: MMClipAnalyzer::category_for_tags(tags);
		}

		// Anything the caller did not classify gets classified from its motion.
		if (tags == 0) {
			const Dictionary stats = analyze_animation(p_skeleton, animation);
			tags = _analyzer->classify_dictionary(stats, name);
			category = MMClipAnalyzer::category_for_tags(tags);
		}

		append_animation(database, p_skeleton, animation, name, String(), category, tags);
	}

	_progress = 1.0f;
	_progress_label = "done";

	if (database->get_frame_count() > 0) {
		database->finalize(_schema->get_dimension());
	}
	return database;
}

int MMFeatureExtractor::guess_tags_from_name(const String &p_name) {
	// A scratch analyzer with motion analysis disabled: this makes the name
	// rules (if any are configured on the default MMClipAnalyzer) the only
	// source of tags. With no rules configured — the shipped default — this
	// truthfully returns MM_TAG_IDLE rather than guessing at motion that was
	// never measured. Real, motion-based tags come from analyze_library() /
	// build_database() once a skeleton is available.
	Ref<MMClipAnalyzer> analyzer;
	analyzer.instantiate();
	analyzer->set_use_name_rules(true);
	analyzer->set_use_motion_analysis(false);

	const MMClipStats empty_stats;
	return analyzer->classify(empty_stats, p_name);
}

int MMFeatureExtractor::guess_category_from_tags(int p_tags) {
	return MMClipAnalyzer::category_for_tags(p_tags);
}

void MMFeatureExtractor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_schema", "schema"), &MMFeatureExtractor::set_schema);
	ClassDB::bind_method(D_METHOD("get_schema"), &MMFeatureExtractor::get_schema);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "schema", PROPERTY_HINT_RESOURCE_TYPE, "MMFeatureSchema"),
			"set_schema", "get_schema");

	ClassDB::bind_method(D_METHOD("set_profile", "profile"), &MMFeatureExtractor::set_profile);
	ClassDB::bind_method(D_METHOD("get_profile"), &MMFeatureExtractor::get_profile);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "profile", PROPERTY_HINT_RESOURCE_TYPE,
						  "MMSkeletonProfile"),
			"set_profile", "get_profile");

	ClassDB::bind_method(D_METHOD("set_analyzer", "analyzer"), &MMFeatureExtractor::set_analyzer);
	ClassDB::bind_method(D_METHOD("get_analyzer"), &MMFeatureExtractor::get_analyzer);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "analyzer", PROPERTY_HINT_RESOURCE_TYPE,
						  "MMClipAnalyzer"),
			"set_analyzer", "get_analyzer");

	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::FLOAT, sample_rate)
	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::FLOAT, foot_contact_speed_ratio)
	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::FLOAT, foot_contact_height_ratio)
	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::BOOL, auto_detect_profile)
	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::BOOL, auto_configure_schema)
	MM_BIND_PROPERTY(MMFeatureExtractor, Variant::FLOAT, root_yaw_offset_degrees)

	ClassDB::bind_method(D_METHOD("analyze_animation", "skeleton", "animation"),
			&MMFeatureExtractor::analyze_animation);
	ClassDB::bind_method(D_METHOD("analyze_library", "skeleton", "library"),
			&MMFeatureExtractor::analyze_library);
	ClassDB::bind_method(D_METHOD("build_database", "skeleton", "library", "clip_settings"),
			&MMFeatureExtractor::build_database);
	ClassDB::bind_method(D_METHOD("append_animation", "database", "skeleton", "animation", "name",
								 "library", "category", "tags"),
			&MMFeatureExtractor::append_animation);
	ClassDB::bind_method(D_METHOD("get_progress"), &MMFeatureExtractor::get_progress);
	ClassDB::bind_method(D_METHOD("get_progress_label"), &MMFeatureExtractor::get_progress_label);
	ClassDB::bind_method(D_METHOD("get_hip_height"), &MMFeatureExtractor::get_hip_height);

	ClassDB::bind_static_method("MMFeatureExtractor", D_METHOD("guess_tags_from_name", "name"),
			&MMFeatureExtractor::guess_tags_from_name);
	ClassDB::bind_static_method("MMFeatureExtractor", D_METHOD("guess_category_from_tags", "tags"),
			&MMFeatureExtractor::guess_category_from_tags);
}
