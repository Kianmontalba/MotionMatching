#ifndef MM_FEATURE_EXTRACTOR_HPP
#define MM_FEATURE_EXTRACTOR_HPP

#include "clip_analysis.hpp"
#include "feature.hpp"
#include "frame_database.hpp"
#include "mm_types.hpp"
#include "skeleton_profile.hpp"

#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/vector.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMPoseSampler
//
// Resolves an Animation against a Skeleton3D once, then samples it cheaply.
// Track lookup by bone name happens a single time in bind(); after that a
// sample is an array index, not a string compare.
//
// Track paths are matched on their last subname, which is the bone, so a clip
// authored against "Armature/Skeleton3D:Hips" and one authored against
// "%GeneralSkeleton:Hips" both bind without any path rewriting.
// ---------------------------------------------------------------------------
class MMPoseSampler : public RefCounted {
	GDCLASS(MMPoseSampler, RefCounted);

private:
	struct BoneTracks {
		int position = -1;
		int rotation = -1;
		int scale = -1;
	};

	Skeleton3D *_skeleton = nullptr;
	Ref<Animation> _animation;
	Vector<BoneTracks> _tracks;
	Vector<int> _parents;
	Vector<Transform3D> _rest;
	Vector<Transform3D> _local;
	Vector<Transform3D> _model;
	int _bone_count = 0;
	int _root_bone = -1;
	int _fallback_root = -1;
	int _bound_tracks = 0;

protected:
	static void _bind_methods();

public:
	bool bind(Skeleton3D *p_skeleton, const Ref<Animation> &p_animation, const String &p_root_bone,
			const String &p_pelvis_bone);
	void unbind();

	int get_bone_count() const { return _bone_count; }
	int get_root_bone() const { return _root_bone; }
	int get_bound_track_count() const { return _bound_tracks; }

	void sample(double p_time);

	Transform3D get_model_transform(int p_bone) const;
	Transform3D get_local_transform(int p_bone) const;

	// Character space origin. When the clip has no root track the pelvis is
	// projected onto the ground plane and used instead, which is what makes
	// in place animation packs work without conversion.
	Transform3D get_root_transform() const;

	Dictionary sample_bone(const String &p_bone_name, double p_time);
};

// ---------------------------------------------------------------------------
// MMFeatureExtractor
//
// Converts any AnimationLibrary into a MotionMatchingDatabase.
//
// Nothing about a specific animation pack is encoded here. The rig is
// discovered through MMSkeletonProfile, the clips are classified by what they
// do through MMClipAnalyzer, and the feature layout comes from MMFeatureSchema.
// Point it at a Mixamo download, a mocap solve or a studio library and the
// pipeline is identical.
// ---------------------------------------------------------------------------
class MMFeatureExtractor : public RefCounted {
	GDCLASS(MMFeatureExtractor, RefCounted);

private:
	Ref<MMFeatureSchema> _schema;
	Ref<MMSkeletonProfile> _profile;
	Ref<MMClipAnalyzer> _analyzer;

	float _sample_rate = 30.0f;
	float _foot_contact_speed_ratio = 0.35f; // Hip heights per second.
	float _foot_contact_height_ratio = 0.12f; // Hip heights.
	bool _auto_detect_profile = true;
	bool _auto_configure_schema = true;
	// Some rigs bind a root/hip bone whose own rest-pose forward axis does
	// not match the direction the character actually walks (Mixamo-style
	// rigs commonly end up 180 degrees off after import). Rather than
	// guess, this lets the user calibrate it once per skeleton profile.
	float _root_yaw_offset_degrees = 0.0f;

	String _progress_label;
	float _progress = 0.0f;
	float _hip_height = 1.0f;

	Ref<MMPoseSampler> _sampler;
	Vector<int> _pose_bone_indices;
	Vector<int> _foot_slots; // Indices into the schema's pose bone list.

	bool _prepare(Skeleton3D *p_skeleton);
	void _resolve_pose_bones(Skeleton3D *p_skeleton);

protected:
	static void _bind_methods();

	// Override point for custom feature blocks.
	virtual void _extract_extra(float *r_features, int p_count, double p_time);

public:
	void set_schema(const Ref<MMFeatureSchema> &p_schema);
	Ref<MMFeatureSchema> get_schema() const { return _schema; }

	void set_profile(const Ref<MMSkeletonProfile> &p_profile);
	Ref<MMSkeletonProfile> get_profile() const { return _profile; }

	void set_analyzer(const Ref<MMClipAnalyzer> &p_analyzer);
	Ref<MMClipAnalyzer> get_analyzer() const { return _analyzer; }

	MM_ACCESSORS(float, sample_rate)
    MM_ACCESSORS(float, foot_contact_speed_ratio)
	MM_ACCESSORS(float, foot_contact_height_ratio)
	MM_ACCESSORS(bool, auto_detect_profile)
	MM_ACCESSORS(bool, auto_configure_schema)
	MM_ACCESSORS(float, root_yaw_offset_degrees)

	float get_progress() const { return _progress; }
	String get_progress_label() const { return _progress_label; }
	float get_hip_height() const { return _hip_height; }

	// Measures what a clip does. Returned as a dictionary so tools, the editor
	// and GDScript can all reason about it.
	Dictionary analyze_animation(Skeleton3D *p_skeleton, const Ref<Animation> &p_animation);

	// Analyzes every clip in a library and returns the per clip settings the
	// builder consumes. This is the automatic tagging pass; the result is
	// meant to be reviewed and edited, not blindly trusted.
	Dictionary analyze_library(Skeleton3D *p_skeleton, const Ref<AnimationLibrary> &p_library);

	// Main entry point. p_clip_settings is optional; anything missing is
	// filled in by analysis.
	Ref<MotionMatchingDatabase> build_database(Skeleton3D *p_skeleton,
			const Ref<AnimationLibrary> &p_library, const Dictionary &p_clip_settings);

	bool append_animation(const Ref<MotionMatchingDatabase> &p_database, Skeleton3D *p_skeleton,
			const Ref<Animation> &p_animation, const String &p_name, const String &p_library,
			int p_category, int p_tags);

	// Name-only convenience wrappers used before a skeleton is available (the
	// editor's Scan step runs before Build). Both delegate to MMClipAnalyzer,
	// the single canonical classifier — this is not a second classification
	// system, just a static entry point that doesn't require an extractor
	// instance or sampled motion. guess_tags_from_name() disables motion
	// analysis and relies only on name rules, so it truthfully reports
	// MM_TAG_IDLE when no name rule matches rather than inventing motion data
	// that was never measured; the real, motion-based tags are assigned later
	// by analyze_library()/build_database() once a skeleton is scanned.
	static int guess_tags_from_name(const String &p_name);
	static int guess_category_from_tags(int p_tags);
};

} // namespace godot

#endif // MM_FEATURE_EXTRACTOR_HPP
