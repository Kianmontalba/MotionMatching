#ifndef MOTION_MATCHING_HPP
#define MOTION_MATCHING_HPP

#include "cache.hpp"
#include "cost_function.hpp"
#include "feature.hpp"
#include "frame_database.hpp"
#include "mm_extra_database.hpp"
#include "motion_warping.hpp"
#include "pose_search.hpp"
#include "profiler.hpp"
#include "root_motion.hpp"
#include "trajectory.hpp"
#include "traversal.hpp"

#include <godot_cpp/classes/animation_library.hpp>
#include <godot_cpp/classes/animation_node.hpp>
#include <godot_cpp/classes/animation_tree.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/resource.hpp>

namespace godot {

class AnimationNodeMotionMatching;

// ---------------------------------------------------------------------------
// MotionMatchingResource
//
// The asset a designer assigns in the inspector. It carries everything the
// runtime needs, so swapping a weapon set or an AI movement set is a single
// resource swap:
//
//   AnimationTree
//     └── AnimationNodeMotionMatching
//           └── MotionMatchingController
//                 └── MotionMatching.tres  <- this
// ---------------------------------------------------------------------------
class MotionMatchingResource : public Resource {
	GDCLASS(MotionMatchingResource, Resource);

private:
	Ref<AnimationLibrary> _animation_library;
	// Was a single Ref<MotionMatchingDatabase>. Now an MMExtraDatabase --
	// an add-only list of MMBoxAnimation (one per weapon/character set,
	// e.g. "Normal Rifle", "Pistol", "Zombie"), each an add-only list of
	// named MotionMatchingDatabase entries ("Walk", "Sprint", "Jump", ...).
	// Exactly one of those still runs at a time -- see
	// MMExtraDatabase::get_active_database().
	Ref<MMExtraDatabase> _database;
	Ref<MMFeatureSchema> _schema;
	Ref<MMCostFunction> _cost_function;
	// Path (relative to the scene this resource is used in) to the
	// Skeleton3D the database editor should build against. Stored on the
	// resource itself, same as animation_library, so a node picked -- or
	// dropped -- in the Database dock is still there the next time this
	// resource is opened, instead of living only in the dock's own LineEdit.
	NodePath _skeleton_path;

	// Optional manual overrides for just these two roles. Left empty, the
	// extractor's auto-detect decides them like every other role; set by
	// hand here (from the Database dock), MMSkeletonProfile locks them and
	// keeps them through every future auto-detect pass -- these are the only
	// two roles that commonly need a nudge (foot IK/contacts are sensitive to
	// exactly which bone is "the foot"), so this stays two fields instead of
	// the full 22-role manual mapping that used to live in the dock.
	String _left_foot_override;
	String _right_foot_override;

	// Search settings.
	float _search_interval = 0.0f; // 0 = evaluate every frame.
	float _minimum_clip_time = 0.12f;
	float _improvement_threshold = 0.12f; // Required relative gain before switching.
	float _switch_cooldown = 0.05f;
	int _max_comparisons = 0; // 0 = unlimited.
	float _search_budget_msec = 2.0f;
	int _neighborhood_radius = 12;

	// Playback settings.
	float _blend_time = 0.15f;
	float _minimum_blend_time = 0.06f;
	bool _allow_same_clip_jump = false;

	// Trajectory settings.
	float _trajectory_halflife_position = 0.12f;
	float _trajectory_halflife_direction = 0.10f;
	float _max_speed = 6.0f;

	int _quality = MM_QUALITY_ULTRA;

	// Keeps only every Nth frame per clip in the loaded database, to cut RAM
	// on memory constrained devices (see
	// MotionMatchingDatabase::reduce_frame_stride() for the mechanism). 1 =
	// full density, the default and what quality's dimension-truncation
	// already covers on PC. This is a separate axis from quality -- quality
	// trims how many feature dimensions a search compares; this trims how
	// many frames exist to compare against in the first place. Applied once
	// at rebuild(), to the in-memory copy only; the saved .tres is untouched.
	int _frame_stride = 1;
	bool _debug_enabled = false;

protected:
	static void _bind_methods();

public:
	void set_animation_library(const Ref<AnimationLibrary> &p_library);
	Ref<AnimationLibrary> get_animation_library() const { return _animation_library; }

	void set_database(const Ref<MMExtraDatabase> &p_database);
	Ref<MMExtraDatabase> get_database() const { return _database; }

	void set_schema(const Ref<MMFeatureSchema> &p_schema);
	Ref<MMFeatureSchema> get_schema() const { return _schema; }

	void set_cost_function(const Ref<MMCostFunction> &p_cost);
	Ref<MMCostFunction> get_cost_function() const { return _cost_function; }

	MM_ACCESSORS(NodePath, skeleton_path)
	MM_ACCESSORS(String, left_foot_override)
	MM_ACCESSORS(String, right_foot_override)

	MM_ACCESSORS(float, search_interval)
	MM_ACCESSORS(float, minimum_clip_time)
	MM_ACCESSORS(float, improvement_threshold)
	MM_ACCESSORS(float, switch_cooldown)
	MM_ACCESSORS(int, max_comparisons)
	MM_ACCESSORS(float, search_budget_msec)
	MM_ACCESSORS(int, neighborhood_radius)
	MM_ACCESSORS(float, blend_time)
	MM_ACCESSORS(float, minimum_blend_time)
	MM_ACCESSORS(bool, allow_same_clip_jump)
	MM_ACCESSORS(float, trajectory_halflife_position)
	MM_ACCESSORS(float, trajectory_halflife_direction)
	MM_ACCESSORS(float, max_speed)
	MM_ACCESSORS(int, quality)
	MM_ACCESSORS(int, frame_stride)
	MM_ACCESSORS(bool, debug_enabled)

	// Structured pre-flight check, meant to be called once (typically from the
	// editor or on _ready()) rather than every frame. Returns an array of
	// Dictionaries shaped like MMAnimationLibraryTools::validate_library()'s
	// issue list ("severity", "message"), so the same UI pattern can render
	// either. An empty array means nothing was found wrong; it does not by
	// itself prove the resource will produce good matches, only that the
	// pieces are internally consistent enough to try.
	Array validate() const;
};

// ---------------------------------------------------------------------------
// MotionMatchingController
//
// The brain. The player script only reports intent; every animation decision
// happens in here.
//
//   _mm.set_velocity(velocity)
//   _mm.set_desired_velocity(input_direction * target_speed)
//   _mm.set_facing(-global_transform.basis.z)
//   _mm.set_ground_state(is_on_floor())
//   _mm.update(delta)
//
// The controller never plays anything itself. It publishes a decision (clip,
// time, blend weight) that AnimationNodeMotionMatching consumes inside the
// AnimationTree, which is what makes true frame jumping possible.
// ---------------------------------------------------------------------------
class MotionMatchingController : public Node {
	GDCLASS(MotionMatchingController, Node);

private:
	Ref<MotionMatchingResource> _resource;
	Ref<MotionMatchingDatabase> _database;
	Ref<MMFeatureSchema> _schema;
	Ref<MMCostFunction> _cost_function;
	Ref<MMPoseSearch> _search;
	Ref<MMTrajectory> _trajectory;
	Ref<MMRootMotion> _root_motion;

	MMSearchCache _cache;
	MMSearchWorker _worker;
	bool _async_search = false;
	bool _auto_update = true;
	uint64_t _pending_request = 0;
	uint64_t _pending_cache_key = 0;

	// Search-phase profiling. Always created (cheap, and MMProfiler's own
	// design is exactly "attach and read a report"), but only ever fed real
	// numbers when a search actually runs — a cache hit or a skipped frame
	// records nothing, which is the honest thing to report.
	Ref<MMProfiler> _profiler;

	// Lightweight per-phase timings that don't fit MMProfiler's search-shaped
	// Sample struct (query build, continuation evaluation, switch application,
	// total update). Measured with the same Time::get_ticks_usec() pattern
	// pose_search.cpp already uses for search_time_usec, and surfaced through
	// get_debug_info() rather than through MMProfiler, which this leaves
	// untouched.
	double _last_query_build_usec = 0.0;
	double _last_continuation_usec = 0.0;
	double _last_switch_usec = 0.0;
	double _last_update_usec = 0.0;
	float _last_delta_time = 0.0f;

	// Optional traversal detection. Null by default: assigning one is what
	// opts a character into traversal-aware search filtering. No traversal
	// logic runs, and no raycast is performed, unless a caller sets this.
	Ref<MMTraversal> _traversal;

	// Optional motion warping. Null by default; same opt-in pattern as
	// traversal. consume_root_motion() only consults this when it is both
	// assigned and active.
	Ref<MMMotionWarp> _motion_warp;

	NodePath _character_path;
	NodePath _animation_tree_path;
	Node3D *_character = nullptr;
	AnimationTree *_animation_tree = nullptr;
	AnimationNodeMotionMatching *_animation_node = nullptr;

	// Intent, written by the game.
	Vector3 _velocity;
	Vector3 _desired_velocity;
	Vector3 _facing = Vector3(0, 0, -1);
	Vector3 _desired_facing = Vector3(0, 0, -1);
	bool _grounded = true;
	bool _jump_requested = false;
	float _time_since_grounded = 0.0f;
	float _fall_distance = 0.0f;
	float _landing_timer = 0.0f;
	bool _force_search = false;

	// Gameplay gating.
	uint32_t _required_tags = 0;
	uint32_t _blocked_tags = 0;
	uint32_t _category_mask = 0xFFFFFFFFu;
	uint32_t _locked_category = MM_CATEGORY_MAX; // MAX = not locked.
	float _category_lock_timer = 0.0f;

	// Playback state.
	int _current_frame = -1;
	int _previous_frame = -1;
	int _current_animation = -1;
	int _previous_animation = -1;
	StringName _current_clip;
	double _current_time = 0.0;
	StringName _previous_clip;
	double _previous_time = 0.0;
	float _blend_weight = 1.0f;
	float _blend_speed = 0.0f;
	bool _seek_request = false;

	float _time_since_search = 0.0f;
	float _time_in_clip = 0.0f;
	float _switch_cooldown_timer = 0.0f;

	// Scratch buffers, allocated once.
	Vector<float> _query;
	MMSearchStats _stats;
	MMMatchResult _last_result;
	float _continuation_cost = 0.0f;

	// Debug-only: root motion vs. actual character movement. Populated by
	// consume_root_motion() (the delta the animation produced) and by
	// update() (the character's actual world-position change since the
	// previous update() call). The two are one tick offset from each other
	// -- the game applies the consumed delta sometime after this frame's
	// update() runs -- so this is a same-tick-length comparison, not a
	// same-instant one; close enough to show a mismatch (a wall blocking
	// root motion, warp altering it, and so on), not exact frame alignment.
	Vector3 _last_root_motion_delta;
	Vector3 _last_actual_movement_delta;
	Vector3 _previous_character_position;
	bool _has_previous_character_position = false;

	// rebuild() used to call _search->build() directly on whatever thread
	// called it -- for a database with many frames and a wide feature
	// vector (more tracked bones, root velocity, extra dimensions all add
	// up), that is expensive enough to block _ready() for a long time on
	// weak hardware. That block is what was actually behind reports of the
	// game "hanging at the splash screen" instead of starting -- not a
	// crash, just a very long synchronous wait with nothing rendered yet.
	// Building on a thread instead means update()
	// has to explicitly skip searching until the build finishes; see its
	// guard clause and _finish_rebuild_if_ready().
	std::thread _build_thread;
	std::atomic<bool> _build_in_progress{ false };
	// What rebuild() wants to happen once the in-flight build finishes:
	// clearing the cache, resizing the query, rebuilding the cost function,
	// and starting the async search worker (only that last part actually
	// depends on the tree existing; the rest could run immediately, but
	// keeping all of it together in one place is what makes rebuild()
	// safe to call again while a previous build is still running).
	bool _async_search_requested = false;

	void _finish_rebuild_if_ready();

	// Debug-only: per-animation running stats, accumulated as the
	// controller plays, cleared only by reset_debug_analytics(). Key is
	// animation_id (int); value is a Dictionary {"clip_name", "search_count",
	// "total_cost", "play_count"}. search_count/total_cost update on every
	// search this animation's best frame was a valid result for (whether or
	// not it won); play_count updates only when the controller actually
	// switched into it -- see _record_search_cost()/_record_transition().
	Dictionary _animation_stats;

	// Debug-only: rolling log of actual clip switches (not every search),
	// most recent last, capped so a long play session doesn't grow this
	// unbounded.
	Array _transition_log;

	void _record_search_cost(int p_animation_id, float p_cost);
	void _record_transition(const String &p_from_clip, const String &p_to_clip, float p_cost);

	void _sync_from_resource();
	void _bind_animation_tree();
	bool _bind_animation_node(const Ref<AnimationNode> &p_node);
	void _build_query();
	MMSearchFilter _build_filter() const;
	MMSearchContext _build_context() const;
	void _run_search();
	void _consume_result(const MMMatchResult &p_result);
	bool _should_switch(const MMMatchResult &p_candidate) const;
	void _apply_match(const MMMatchResult &p_match);
	void _advance_playback(double p_delta);
	float _evaluate_continuation();
	int _current_database_frame() const;
	uint64_t _make_cache_key(const MMSearchFilter &p_filter) const;
	void _evaluate_traversal();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	MotionMatchingController();
	~MotionMatchingController();

	void _ready() override;
	void _process(double p_delta) override;
	void _physics_process(double p_delta) override;

	void set_resource(const Ref<MotionMatchingResource> &p_resource);
	Ref<MotionMatchingResource> get_resource() const { return _resource; }

	void set_character_path(const NodePath &p_path);
	NodePath get_character_path() const { return _character_path; }

	void set_animation_tree_path(const NodePath &p_path);
	NodePath get_animation_tree_path() const { return _animation_tree_path; }

	MM_ACCESSORS(bool, async_search)
	MM_ACCESSORS(bool, auto_update)

	// ---- Intent API, called by the player or the AI ----
	void set_velocity(const Vector3 &p_velocity);
	Vector3 get_velocity() const { return _velocity; }
	void set_desired_velocity(const Vector3 &p_velocity);
	void set_direction(const Vector3 &p_direction); // Shorthand for desired facing.
	void set_facing(const Vector3 &p_facing);
	void set_ground_state(bool p_grounded);
	bool get_ground_state() const { return _grounded; }
	void set_fall_distance(float p_distance);
	void request_jump();

	// Feeds an externally computed trajectory, for AI navigation paths or for
	// a networked replay. Overrides the internal predictor for this tick.
	void set_trajectory(const PackedVector3Array &p_positions, const PackedVector3Array &p_directions);
	Ref<MMTrajectory> get_trajectory() const { return _trajectory; }

	void set_required_tags(int p_tags);
	int get_required_tags() const { return (int)_required_tags; }
	void set_blocked_tags(int p_tags);
	int get_blocked_tags() const { return (int)_blocked_tags; }
	void set_category_mask(int p_mask);
	int get_category_mask() const { return (int)_category_mask; }

	// Locks the search to one category until it is released or the timer runs
	// out. This is what stops a jump from being interrupted by a walk clip.
	void lock_category(int p_category, float p_duration);
	void release_category();

	// Main tick. Call it manually when auto_update is off.
	void update(double p_delta);
	void force_search();

	// ---- Output, consumed by AnimationNodeMotionMatching ----
	StringName get_current_clip() const { return _current_clip; }
	double get_current_time() const { return _current_time; }
	StringName get_previous_clip() const { return _previous_clip; }
	double get_previous_time() const { return _previous_time; }
	float get_blend_weight() const { return _blend_weight; }
	bool take_seek_request();

	int get_current_frame() const { return _current_frame; }

	// Foot contact of the currently playing frame, read from the same data
	// the feature extractor wrote during database build. Split out of
	// get_debug_info() so a per-frame consumer (the 3D debug draw) doesn't
	// have to build the whole info Dictionary just for these two bits.
	bool get_current_left_foot_contact() const;
	bool get_current_right_foot_contact() const;
	int get_current_animation_id() const { return _current_animation; }
	Ref<MMRootMotion> get_root_motion() const { return _root_motion; }
	Transform3D consume_root_motion();

	Dictionary get_debug_info() const;
	PackedVector3Array get_debug_trajectory() const;

	// Debug-only: the p_count best-matching frames by exact cost, using the
	// same filter the most recent search used. O(database frame count) per
	// call -- fine for a debug panel polling a few times a second, not for
	// every gameplay frame. See
	// MMPoseSearch::search_top_candidates_debug_raw() for why the cost here
	// is exact rather than the fast path's pruned value.
	Array get_debug_candidates(int p_count = 3) const;

	// Debug-only: each feature group's share of the winning frame's total
	// cost, as a percentage -- e.g. {"pose_position": 40.0, "trajectory_position":
	// 35.0, ...}. Empty if nothing has matched yet.
	Dictionary get_debug_cost_breakdown() const;

	// Debug-only: next foot-contact change within the CURRENTLY PLAYING
	// clip (not future trajectory points, whose clip hasn't been chosen
	// yet). {"next_left_change_seconds": ..., "next_right_change_seconds":
	// ...}; a key is absent if no change is found within the look-ahead
	// horizon or the clip ends first.
	Dictionary get_debug_footstep_timing() const;

	// Debug-only: total vs. filter-surviving frame count for the filter the
	// most recent search used -- see MMPoseSearch::count_filtered_frames_debug_raw().
	Dictionary get_debug_filter_stats() const;

	// Debug-only: per-animation search-cost and usage stats, accumulated
	// since the last reset_debug_analytics() call (or controller start).
	// {animation_id (int): {"clip_name", "search_count", "average_cost",
	// "play_count"}, ...}. This is the same underlying data a "pose
	// matching heatmap" would visualize (average_cost per animation) and
	// what a "most/least used" readout would rank (play_count) -- one
	// accumulator, two ways to read it.
	Dictionary get_debug_analytics() const;

	// Debug-only: rolling log of actual clip switches, most recent last.
	// [{"time", "from_clip", "to_clip", "cost"}, ...]. Only real switches
	// (_apply_match() actually changing clip), not every search.
	Array get_debug_transitions() const;

	// Clears get_debug_analytics()/get_debug_transitions()'s accumulated
	// history. Does not affect playback.
	void reset_debug_analytics();

	// ---- Optional subsystems, opt-in by assignment ----

	// Search-phase profiler. Always present (see the member comment), but
	// only meaningful once search has actually run a few times.
	Ref<MMProfiler> get_profiler() const { return _profiler; }

	// Assigning a traversal instance opts the controller into probing the
	// predicted trajectory for obstacles once per grounded update tick, and
	// biasing the next search's category/tags toward whatever it finds.
	// Leaving this null (the default) costs nothing — no probe runs.
	void set_traversal(const Ref<MMTraversal> &p_traversal);
	Ref<MMTraversal> get_traversal() const { return _traversal; }

	// Assigning a motion warp instance opts the controller into applying it
	// through consume_root_motion() while active. begin_warp() opens a single
	// window spanning from the current playback time to the end of the
	// currently playing clip — a caller wanting a different window should add
	// it directly via get_motion_warp() before or instead of calling
	// begin_warp(). end_warp() is not called automatically; the caller decides
	// when the warp is done.
	void set_motion_warp(const Ref<MMMotionWarp> &p_warp);
	Ref<MMMotionWarp> get_motion_warp() const { return _motion_warp; }
	void begin_warp(const Transform3D &p_target);
	void end_warp();
	bool is_warping() const { return _motion_warp.is_valid() && _motion_warp->is_active(); }

	// Rebuilds the acceleration structure. Call after swapping the database.
	void rebuild();

	// Switches the active database by its script-facing grouping tag
	// instead of a hardcoded box/database name -- e.g.
	// `mm_controller.play_by_tag(1)` to activate whichever database(s) were
	// tagged "1" in the dock ("Stand"/"Walk"/"Run"/"Sprint" could all share
	// tag 1). If more than one database shares the tag, the one whose
	// MotionMatchingDatabase::get_speed_range() best fits the controller's
	// current speed (from set_velocity()/set_desired_velocity()) is picked.
	// Returns false and leaves playback unchanged if no database has this
	// tag, or if no resource/database is assigned yet.
	bool play_by_tag(int p_tag);
};

} // namespace godot

#endif // MOTION_MATCHING_HPP
