#include "motion_matching.hpp"

#include "animation_node_motion_matching.hpp"

#include <godot_cpp/classes/animation_node_blend_tree.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_direct_space_state3d.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

MotionMatchingController::MotionMatchingController() {
	_trajectory.instantiate();
	_root_motion.instantiate();
	_search.instantiate();
	_cache.resize(1024);
	_profiler.instantiate();
}

MotionMatchingController::~MotionMatchingController() {
	_worker.stop();
}

void MotionMatchingController::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		_worker.stop();
	}
}

void MotionMatchingController::_ready() {
	if (Engine::get_singleton()->is_editor_hint()) {
		set_process(false);
		set_physics_process(false);
		return;
	}

	if (!_character_path.is_empty()) {
		_character = Object::cast_to<Node3D>(get_node_or_null(_character_path));
	}
	if (_character == nullptr) {
		_character = Object::cast_to<Node3D>(get_parent());
	}
	if (_character == nullptr) {
		WARN_PRINT_ONCE(
				"MotionMatchingController has no resolvable character (character_path is empty or "
				"invalid, and the parent node is not a Node3D). Trajectory prediction, traversal "
				"probing, and root motion warping will all behave as if the character never moves.");
	}

	_sync_from_resource();
	_bind_animation_tree();
	rebuild();

	set_physics_process(_auto_update);
}

void MotionMatchingController::_process(double p_delta) {
	// Playback advances on the render tick so the pose stays smooth even when
	// physics runs at a lower rate. Searching happens in _physics_process.
}

void MotionMatchingController::_physics_process(double p_delta) {
	if (_auto_update) {
		update(p_delta);
	}
}

void MotionMatchingController::_sync_from_resource() {
	if (_resource.is_null()) {
		return;
	}

	// MotionMatchingResource now holds an MMExtraDatabase (boxes of named
	// databases) instead of a single flat database directly. The controller
	// itself is unchanged: it still runs against exactly one
	// MotionMatchingDatabase, whichever one MMExtraDatabase currently marks
	// active.
	Ref<MMExtraDatabase> extra_database = _resource->get_database();
	_database = extra_database.is_valid() ? extra_database->get_active_database() : Ref<MotionMatchingDatabase>();
	_schema = _resource->get_schema();
	if (_schema.is_null() && _database.is_valid()) {
		_schema = _database->get_schema();
	}
	if (_schema.is_null()) {
		_schema = MMFeatureSchema::make_default();
	}

	_cost_function = _resource->get_cost_function();
	if (_cost_function.is_null()) {
		_cost_function.instantiate();
	}
	_cost_function->rebuild(_schema);

	_trajectory->configure(_schema);
	_trajectory->set_halflife_position(_resource->get_trajectory_halflife_position());
	_trajectory->set_halflife_direction(_resource->get_trajectory_halflife_direction());
	_trajectory->set_max_speed(_resource->get_max_speed());

	_root_motion->set_database(_database);
	_query.resize(_schema->get_dimension());
}

void MotionMatchingController::_bind_animation_tree() {
	if (_animation_tree_path.is_empty()) {
		return;
	}
	_animation_tree = Object::cast_to<AnimationTree>(get_node_or_null(_animation_tree_path));
	if (_animation_tree == nullptr) {
		return;
	}
	_bind_animation_node(Ref<AnimationNode>(_animation_tree->get_tree_root().ptr()));
}

// Walks the tree so the node can sit anywhere: as the root, or as one node
// inside a BlendTree with a turn in place layer on top of it.
bool MotionMatchingController::_bind_animation_node(const Ref<AnimationNode> &p_node) {
	if (p_node.is_null()) {
		return false;
	}

	Ref<AnimationNodeMotionMatching> node = p_node;
	if (node.is_valid()) {
		node->bind_controller(this);
		_animation_node = node.ptr();
		return true;
	}

	// NOTE: recursing into an AnimationNodeBlendTree to find a nested
	// AnimationNodeMotionMatching automatically is not supported, because
	// AnimationNodeBlendTree::get_node_list() is not exposed by the
	// godot-cpp API version this addon is built against. If this node is
	// not the AnimationTree's root, assign animation_tree_path so it points
	// somewhere the node can be resolved directly instead.
	return false;
}

void MotionMatchingController::set_resource(const Ref<MotionMatchingResource> &p_resource) {
	_resource = p_resource;
	if (is_inside_tree()) {
		// Stop the worker before anything below can reassign/drop the Refs
		// (_cost_function, _database, _schema) it may still be holding a raw
		// pointer into from an in-flight async search. rebuild() restarts it
		// once the new resource's objects are in place.
		_worker.stop();
		_sync_from_resource();
		rebuild();
	}
}

void MotionMatchingController::set_character_path(const NodePath &p_path) {
	_character_path = p_path;
	if (is_inside_tree()) {
		_character = Object::cast_to<Node3D>(get_node_or_null(p_path));
	}
}

void MotionMatchingController::set_animation_tree_path(const NodePath &p_path) {
	_animation_tree_path = p_path;
	if (is_inside_tree()) {
		_bind_animation_tree();
	}
}

void MotionMatchingController::rebuild() {
	if (_database.is_null() || _database->get_frame_count() == 0) {
		return;
	}

	if (!_database->is_format_compatible()) {
		WARN_PRINT_ONCE(vformat(
				"Motion matching database format_version (%d) does not match what this build of "
				"the addon expects (%d). It was likely built by a different addon version; "
				"rebuilding it from the editor is recommended.",
				_database->get_format_version(), MM_DATABASE_FORMAT_VERSION));
	}
	if (_schema.is_valid() && _schema->get_dimension() != _database->get_dimension()) {
		WARN_PRINT_ONCE(vformat(
				"Schema dimension (%d) does not match database dimension (%d); search results "
				"will likely be meaningless. Rebuild the database with the schema currently "
				"assigned, or assign the schema this database was actually built with.",
				_schema->get_dimension(), _database->get_dimension()));
	}

	_worker.stop();
	_search->build(_database);
	_cache.clear();
	_query.resize(_database->get_dimension());

	if (_cost_function.is_valid()) {
		_cost_function->rebuild(_schema);
	}
	if (_async_search) {
		_worker.start(_search.ptr(), _cost_function.ptr());
	}

	_current_frame = -1;
	_force_search = true;
}

// ---------------------------------------------------------------------------
// Intent API
// ---------------------------------------------------------------------------

void MotionMatchingController::set_velocity(const Vector3 &p_velocity) {
	_velocity = p_velocity;
}

void MotionMatchingController::set_desired_velocity(const Vector3 &p_velocity) {
	_desired_velocity = p_velocity;
}

void MotionMatchingController::set_direction(const Vector3 &p_direction) {
	set_facing(p_direction);
	_desired_facing = p_direction.length_squared() > 0.000001f ? p_direction.normalized() : _desired_facing;
}

void MotionMatchingController::set_facing(const Vector3 &p_facing) {
	if (p_facing.length_squared() > 0.000001f) {
		_facing = p_facing.normalized();
	}
}

void MotionMatchingController::set_ground_state(bool p_grounded) {
	if (p_grounded && !_grounded) {
		// Touchdown. Open a short window in which landing clips are the only
		// sensible answer, then let locomotion take over again.
		_landing_timer = 0.25f;
		_force_search = true;
	} else if (!p_grounded && _grounded) {
		_force_search = true;
	}
	_grounded = p_grounded;
}

void MotionMatchingController::set_fall_distance(float p_distance) {
	_fall_distance = p_distance;
}

void MotionMatchingController::request_jump() {
	if (!_grounded) {
		return;
	}
	_jump_requested = true;
	_force_search = true;
}

void MotionMatchingController::set_trajectory(const PackedVector3Array &p_positions,
		const PackedVector3Array &p_directions) {
	_trajectory->set_external_samples(p_positions, p_directions);
}

void MotionMatchingController::set_traversal(const Ref<MMTraversal> &p_traversal) {
	_traversal = p_traversal;
}

void MotionMatchingController::set_motion_warp(const Ref<MMMotionWarp> &p_warp) {
	_motion_warp = p_warp;
}

void MotionMatchingController::begin_warp(const Transform3D &p_target) {
	if (_motion_warp.is_null() || _character == nullptr || _current_animation < 0 || _database.is_null()) {
		return;
	}
	Ref<MMAnimationEntry> entry = _database->get_animation_entry(_current_animation);
	// Defaults to warping for the rest of the currently playing clip, since
	// that is the only clip-length information the controller has without
	// assuming anything about what the target represents. A caller that wants
	// a different window can call get_motion_warp()->add_window() directly
	// instead of (or in addition to) this.
	const float end_time = entry.is_valid() ? (float)entry->get_length() : (float)_current_time;

	_motion_warp->clear_windows();
	_motion_warp->begin(_character->get_global_transform());
	_motion_warp->add_window((float)_current_time, end_time, p_target);
}

void MotionMatchingController::end_warp() {
	if (_motion_warp.is_valid()) {
		_motion_warp->end();
	}
}

void MotionMatchingController::set_required_tags(int p_tags) {
	_required_tags = (uint32_t)p_tags;
}

void MotionMatchingController::set_blocked_tags(int p_tags) {
	_blocked_tags = (uint32_t)p_tags;
}

void MotionMatchingController::set_category_mask(int p_mask) {
	_category_mask = (uint32_t)p_mask;
}

void MotionMatchingController::lock_category(int p_category, float p_duration) {
	_locked_category = (uint32_t)p_category;
	_category_lock_timer = p_duration;
}

void MotionMatchingController::release_category() {
	_locked_category = MM_CATEGORY_MAX;
	_category_lock_timer = 0.0f;
}

void MotionMatchingController::force_search() {
	_force_search = true;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void MotionMatchingController::update(double p_delta) {
	if (_database.is_null() || _database->get_frame_count() == 0 || _schema.is_null()) {
		WARN_PRINT_ONCE(
				"MotionMatchingController::update() called with no usable database/schema "
				"(resource unassigned, database empty, or rebuild() never called). The "
				"controller will not search or advance playback until this is fixed.");
		return;
	}

	const uint64_t update_started = Time::get_singleton()->get_ticks_usec();
	const float delta = (float)p_delta;
	_last_delta_time = delta;

	// Debug-only: how far the character actually moved since the previous
	// update() call, for comparison against the root motion the animation
	// produced (see consume_root_motion()'s comment for the one-tick offset
	// this comparison carries).
	if (_character != nullptr) {
		const Vector3 current_position = _character->get_global_transform().origin;
		if (_has_previous_character_position) {
			_last_actual_movement_delta = current_position - _previous_character_position;
		}
		_previous_character_position = current_position;
		_has_previous_character_position = true;
	}

	_time_since_search += delta;
	_time_in_clip += delta;
	if (_switch_cooldown_timer > 0.0f) {
		_switch_cooldown_timer -= delta;
	}
	if (_landing_timer > 0.0f) {
		_landing_timer -= delta;
	}
	if (_category_lock_timer > 0.0f) {
		_category_lock_timer -= delta;
		if (_category_lock_timer <= 0.0f) {
			release_category();
		}
	}
	_time_since_grounded = _grounded ? 0.0f : _time_since_grounded + delta;

	// Trajectory first: the search is only as good as the intent it is given.
	const Transform3D character_transform =
			_character != nullptr ? _character->get_global_transform() : Transform3D();
	_trajectory->set_state(character_transform.origin, _velocity, _facing);
	_trajectory->set_desired_velocity(_desired_velocity);
	_trajectory->set_desired_facing(_desired_facing);
	_trajectory->update(p_delta);

	// Optional: only probes when a traversal instance has been assigned.
	_evaluate_traversal();

	_advance_playback(p_delta);

	// Collect an async result before deciding whether to ask another question.
	if (_async_search && _worker.is_running()) {
		MMSearchWorker::Response response;
		if (_worker.poll(response) && response.id == _pending_request) {
			_stats = response.stats;
			_profiler->record(_stats.search_time_usec, _stats.frames_compared,
					_stats.candidates_visited, _stats.nodes_visited, _stats.budget_exceeded);
			_consume_result(response.result);
			_pending_request = 0;
		}
	}

	const float interval = _resource.is_valid() ? _resource->get_search_interval() : 0.0f;
	const bool due = _force_search || _current_frame < 0 || _time_since_search >= interval;

	if (due) {
		_run_search();
		_time_since_search = 0.0f;
		_force_search = false;
	}

	const int previous_frame = _blend_weight < 1.0f ? _previous_frame : -1;
	_root_motion->update(p_delta, _current_frame, previous_frame, _blend_weight);

	_last_update_usec = (double)(Time::get_singleton()->get_ticks_usec() - update_started);
}

void MotionMatchingController::_build_query() {
	const int dimension = _schema->get_dimension();
	if (_query.size() != dimension) {
		_query.resize(dimension);
	}

	float *query = _query.ptrw();
	for (int i = 0; i < dimension; i++) {
		query[i] = 0.0f;
	}

	// The character basis is yaw only. Pitch and roll never belong in a
	// locomotion query; they would make a slope invalidate every match.
	Vector3 forward = _facing;
	forward.y = 0.0f;
	if (forward.length_squared() < 0.000001f) {
		forward = Vector3(0, 0, -1);
	} else {
		forward.normalize();
	}
	const Basis basis = Basis::looking_at(forward, Vector3(0, 1, 0));

	_trajectory->write_features(query, _schema, basis);

	if (_schema->get_include_root_velocity()) {
		const Vector3 local_velocity = basis.inverse().xform(_velocity);
		const int offset = _schema->get_root_velocity_offset();
		query[offset + 0] = local_velocity.x;
		query[offset + 1] = local_velocity.y;
		query[offset + 2] = local_velocity.z;
	}

	_database->normalize_query_ptr(query);

	// The pose half of the query is the pose the character is actually in.
	// Reading it straight from the frame that is playing is both free and
	// exact, and it is what makes the next match continue the motion rather
	// than restart it.
	if (_current_frame >= 0 && _current_frame < _database->get_frame_count()) {
		const float *current = _database->get_frame_features(_current_frame);
		const int start = _schema->get_bone_position_offset();
		for (int i = start; i < dimension; i++) {
			query[i] = current[i];
		}
	}
}

MMSearchFilter MotionMatchingController::_build_filter() const {
	MMSearchFilter filter;
	filter.required_tags = _required_tags;
	filter.blocked_tags = _blocked_tags;
	filter.category_mask = _category_mask;

	if (_locked_category != MM_CATEGORY_MAX) {
		filter.category_mask = 1u << _locked_category;
		return filter;
	}

	// Optional: only ever non-NONE when a traversal instance is assigned and
	// _evaluate_traversal() found an obstacle this tick. The specific tags
	// required for each traversal type are computed by MMTraversal itself
	// (get_required_tags()), not decided here — this is wiring, not gameplay
	// policy.
	if (_traversal.is_valid() && _traversal->get_detected_type() != MMTraversal::TRAVERSAL_NONE) {
		filter.category_mask = 1u << MM_CATEGORY_TRAVERSAL;
		filter.any_tags = (uint32_t)_traversal->get_required_tags();
		return filter;
	}

	if (_jump_requested && _grounded) {
		filter.category_mask = 1u << MM_CATEGORY_AIRBORNE;
		filter.any_tags = MM_TAG_JUMP;
		return filter;
	}

	if (!_grounded) {
		filter.category_mask = 1u << MM_CATEGORY_AIRBORNE;
		// A short coyote window still allows the takeoff clip; after that only
		// falling makes sense.
		filter.any_tags = _time_since_grounded > 0.35f ? (uint32_t)MM_TAG_FALL
													   : (uint32_t)(MM_TAG_JUMP | MM_TAG_FALL);
		return filter;
	}

	if (_landing_timer > 0.0f) {
		filter.category_mask = (1u << MM_CATEGORY_AIRBORNE) | (1u << MM_CATEGORY_LOCOMOTION);
		filter.any_tags = MM_TAG_LAND | MM_TAG_ROLL | MM_TAG_IDLE | MM_TAG_WALK | MM_TAG_JOG |
				MM_TAG_RUN | MM_TAG_SPRINT;
		return filter;
	}

	// Grounded and settled: airborne and traversal clips are off the table
	// unless a gameplay system explicitly opens them.
	filter.category_mask &= ~(1u << MM_CATEGORY_AIRBORNE);
	filter.category_mask &= ~(1u << MM_CATEGORY_TRAVERSAL);
	filter.blocked_tags |= MM_TAG_JUMP | MM_TAG_FALL;
	return filter;
}

MMSearchContext MotionMatchingController::_build_context() const {
	MMSearchContext context;
	context.continuation_frame = _current_database_frame();
	context.previous_frame = _last_result.frame_index;
	context.neighborhood_radius = _resource.is_valid() ? _resource->get_neighborhood_radius() : 12;
	context.max_comparisons = _resource.is_valid() ? _resource->get_max_comparisons() : 0;
	return context;
}

int MotionMatchingController::_current_database_frame() const {
	if (_current_animation < 0) {
		return -1;
	}
	return _database->get_frame_at_time(_current_animation, (float)_current_time);
}

// Optional. Does nothing unless a traversal instance has been assigned and
// the character is on the ground and resolvable — a probe while airborne, or
// with no character to probe from, would not mean anything.
void MotionMatchingController::_evaluate_traversal() {
	if (_traversal.is_null()) {
		return;
	}
	if (_character == nullptr || !_grounded) {
		_traversal->clear();
		return;
	}
	Ref<World3D> world = _character->get_world_3d();
	if (world.is_null()) {
		return;
	}
	PhysicsDirectSpaceState3D *space = world->get_direct_space_state();
	if (space == nullptr) {
		return;
	}

	const MMTraversal::TraversalType detected =
			_traversal->probe(space, _character->get_global_transform(), _trajectory);
	if (detected != MMTraversal::TRAVERSAL_NONE) {
		// The game decides what a detected traversal actually means — start a
		// motion warp toward it, play a one-shot animation, ignore it, etc.
		// The controller only reports what geometry it found.
		emit_signal("traversal_requested", (int)detected, _traversal->get_top_point());
	}
}

float MotionMatchingController::_evaluate_continuation() {
	const int frame = _current_database_frame();
	if (frame < 0) {
		return MM_INFINITY;
	}
	const int dimension = _database->get_dimension();
	return _cost_function->compute_raw(_query.ptr(), _database->get_frame_features(frame),
			_cost_function->get_dimension_weights(), dimension, MM_INFINITY);
}

void MotionMatchingController::_run_search() {
	if (!_search->is_built() || _cost_function.is_null()) {
		WARN_PRINT_ONCE(
				"MotionMatchingController::_run_search() called before the search tree was built "
				"or with no cost function. Call rebuild() after assigning a resource/database.");
		return;
	}

	uint64_t started = Time::get_singleton()->get_ticks_usec();
	_build_query();
	_last_query_build_usec = (double)(Time::get_singleton()->get_ticks_usec() - started);

	started = Time::get_singleton()->get_ticks_usec();
	_continuation_cost = _evaluate_continuation();
	_last_continuation_usec = (double)(Time::get_singleton()->get_ticks_usec() - started);

	const MMSearchFilter filter = _build_filter();
	MMSearchContext context = _build_context();

	// Handing the continuation cost to the search as an upper bound means the
	// tree can immediately discard every branch that cannot beat what the
	// character is already doing.
	if (_continuation_cost < MM_INFINITY) {
		context.best_cost_seed = _continuation_cost;
	}

	// Cache lookup. The key mixes the quantized query with the filter, so a
	// change in gameplay state (a jump request, a category lock, a traversal
	// bias, ...) always misses even if the query itself repeats — reusing an
	// answer computed under a different filter would silently let through a
	// frame that should have been excluded, which is exactly the search
	// quality regression the cache must not cause.
	_pending_cache_key = _make_cache_key(filter);
	int cached_frame = -1;
	float cached_cost = MM_INFINITY;
	if (_cache.lookup(_pending_cache_key, cached_frame, cached_cost) && cached_frame >= 0 &&
			cached_frame < _database->get_frame_count()) {
		MMMatchResult result;
		result.frame_index = cached_frame;
		result.animation_id = _database->get_frame_animation_id(cached_frame);
		result.animation_time = _database->get_frame_time_value(cached_frame);
		result.normalized_time = _database->get_frame_normalized_value(cached_frame);
		result.cost = cached_cost;
		_stats.reset();
		_stats.cache_hits = _cache.get_hits();
		_stats.cache_misses = _cache.get_misses();
		// A cache hit did no tree work, so it is deliberately not fed to
		// MMProfiler: recording it as a near-zero-cost search sample would
		// dilute the percentiles the profiler exists to surface.
		_consume_result(result);
		return;
	}

	// Mobile/low-end LOD: MotionMatchingResource::quality picks how many of
	// the schema's leading feature dimensions are actually compared, per
	// MMFeatureSchema::get_lod_dimension()'s documented prefix ordering.
	// This was previously a dead property -- set_quality()/get_quality()
	// existed and get_lod_dimension() computed the right number, but
	// nothing ever read either at search time, so every device always ran
	// the full ULTRA-dimension search regardless of this setting.
	const int lod_dimension = (_resource.is_valid() && _schema.is_valid())
			? _schema->get_lod_dimension(_resource->get_quality())
			: -1;

	if (_async_search && _worker.is_running()) {
		_pending_request = _worker.submit(_query.ptr(), _query.size(), filter, context, lod_dimension);
		return;
	}

	const MMMatchResult result = _search->search(_query.ptr(), _cost_function, filter, context, _stats, lod_dimension);
	_stats.cache_hits = _cache.get_hits();
	_stats.cache_misses = _cache.get_misses();
	_profiler->record(_stats.search_time_usec, _stats.frames_compared, _stats.candidates_visited,
			_stats.nodes_visited, _stats.budget_exceeded);
	_consume_result(result);
}

// Combines the quantized query hash with the parts of the filter that change
// which frames are eligible. Boost::hash_combine-style mixing: cheap, and
// enough to guarantee a different filter produces a different key even when
// individual fields happen to XOR to the same value.
uint64_t MotionMatchingController::_make_cache_key(const MMSearchFilter &p_filter) const {
	// Normalized feature space is roughly unit variance per group, so a
	// quantization step of 0.15 (about a seventh of one standard deviation)
	// groups queries that are close enough to share an answer without
	// blurring together motions that should be treated differently. This is
	// an internal constant rather than an exposed property to keep this
	// integration minimal; see BUILD_READINESS_REPORT.md for the tradeoff.
	constexpr float MM_CACHE_QUANTIZATION = 0.15f;

	uint64_t key = MMSearchCache::make_key(_query.ptr(), _query.size(), MM_CACHE_QUANTIZATION);
	auto mix = [](uint64_t h, uint32_t v) -> uint64_t {
		return h ^ ((uint64_t)v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2));
	};
	key = mix(key, p_filter.required_tags);
	key = mix(key, p_filter.any_tags);
	key = mix(key, p_filter.blocked_tags);
	key = mix(key, p_filter.category_mask);
	return key;
}

void MotionMatchingController::_consume_result(const MMMatchResult &p_result) {
	_last_result = p_result;
	if (!p_result.is_valid()) {
		// Nothing passed the filter. Keep playing rather than freezing; the
		// fallback is always the pose we already have.
		return;
	}
	// Debug-only, and deliberately here rather than in _apply_match() below:
	// this records how this animation's best frame scored on THIS search,
	// whether or not it actually wins the switch decision -- that is what
	// makes the accumulated average a "how competitive is this animation"
	// signal instead of just "how often did it win."
	_record_search_cost(p_result.animation_id, p_result.cost);
	_cache.store(_pending_cache_key, p_result.frame_index, p_result.cost);
	if (_should_switch(p_result)) {
		_apply_match(p_result);
	}
}

bool MotionMatchingController::_should_switch(const MMMatchResult &p_candidate) const {
	if (_current_frame < 0) {
		return true;
	}

	const int continuation = _current_database_frame();
	if (p_candidate.frame_index == continuation) {
		return false;
	}

	const float minimum_clip_time = _resource.is_valid() ? _resource->get_minimum_clip_time() : 0.12f;
	const float threshold = _resource.is_valid() ? _resource->get_improvement_threshold() : 0.12f;
	const bool allow_same_clip = _resource.is_valid() ? _resource->get_allow_same_clip_jump() : false;

	// A jump request or a state change is a gameplay decision, not a cost
	// decision, so it bypasses the dampers.
	if (_jump_requested || _landing_timer > 0.0f) {
		return true;
	}

	if (_time_in_clip < minimum_clip_time) {
		return false;
	}
	if (_switch_cooldown_timer > 0.0f) {
		return false;
	}
	if (!allow_same_clip && p_candidate.animation_id == _current_animation) {
		return false;
	}
	if (_continuation_cost >= MM_INFINITY) {
		return true;
	}

	// Hysteresis. The candidate has to be meaningfully better, not marginally
	// better, or the character flickers between near identical clips.
	return p_candidate.cost < _continuation_cost * (1.0f - threshold);
}

void MotionMatchingController::_apply_match(const MMMatchResult &p_match) {
	const uint64_t started = Time::get_singleton()->get_ticks_usec();

	Ref<MMAnimationEntry> entry = _database->get_animation_entry(p_match.animation_id);
	if (entry.is_null()) {
		WARN_PRINT_ONCE(
				"MotionMatchingController::_apply_match() received a match with an animation_id "
				"that has no corresponding MMAnimationEntry in the database. This indicates the "
				"database and the search tree built from it are out of sync — call rebuild().");
		return;
	}

	const float blend_time = _resource.is_valid() ? _resource->get_blend_time() : 0.15f;
	const float minimum_blend = _resource.is_valid() ? _resource->get_minimum_blend_time() : 0.06f;
	const float cooldown = _resource.is_valid() ? _resource->get_switch_cooldown() : 0.05f;

	if (_current_animation >= 0) {
		_previous_clip = _current_clip;
		_previous_time = _current_time;
		_previous_frame = _current_frame;
		_previous_animation = _current_animation;
		_blend_weight = 0.0f;
		_blend_speed = 1.0f / MAX(blend_time, minimum_blend);
	} else {
		_blend_weight = 1.0f;
		_blend_speed = 0.0f;
	}

	_current_animation = p_match.animation_id;
	_current_clip = entry->get_qualified_name();

	// Debug-only: logged here (not in _consume_result()) because this is
	// the point where a switch actually happened, not just a candidate that
	// was considered.
	_record_transition(_previous_animation >= 0 ? String(_previous_clip) : String(), _current_clip, p_match.cost);

	// The whole point of motion matching: enter the clip where the motion
	// actually continues, never at zero.
	_current_time = p_match.animation_time;
	_current_frame = p_match.frame_index;
	_seek_request = true;
	_time_in_clip = 0.0f;
	_switch_cooldown_timer = cooldown;

	_root_motion->notify_frame_jump(p_match.frame_index);

	if (_jump_requested) {
		_jump_requested = false;
		lock_category(MM_CATEGORY_AIRBORNE, 0.0f); // Released on touchdown.
	}

	_profiler->record_switch();
	_last_switch_usec = (double)(Time::get_singleton()->get_ticks_usec() - started);

	if (_resource.is_valid() && _resource->get_debug_enabled()) {
		Ref<MMAnimationEntry> previous_entry = _previous_animation >= 0
				? _database->get_animation_entry(_previous_animation)
				: Ref<MMAnimationEntry>();
		// "Transition" isn't a category or tag this addon tracks separately --
		// a transition clip is just an ordinary database entry, matched by
		// cost like any other. This line reports what was actually picked
		// (name, category, tags) so a transition clip either shows up here by
		// name or it doesn't; there is no separate flag to trust instead.
		print_line(vformat(
				"[MotionMatching] Current: %s (category %d, tags %d, loop %s) | Previous: %s | "
				"Velocity: %s (%.2f m/s) | Desired velocity: %s | State: %s",
				entry->get_qualified_name(), entry->get_category(), entry->get_tags(),
				entry->get_loop() ? "true" : "false",
				previous_entry.is_valid() ? previous_entry->get_qualified_name() : StringName("(none)"),
				_velocity, Vector2(_velocity.x, _velocity.z).length(), _desired_velocity,
				!_grounded ? "airborne" : (_landing_timer > 0.0f ? "landing" : "grounded")));
	}

	emit_signal("animation_changed", _current_clip, _current_time, p_match.cost);
}

void MotionMatchingController::_advance_playback(double p_delta) {
	if (_current_animation < 0) {
		return;
	}

	_current_time += p_delta;
	_previous_time += p_delta;

	if (_blend_weight < 1.0f) {
		_blend_weight = MIN(1.0f, _blend_weight + _blend_speed * (float)p_delta);
		if (_previous_animation >= 0) {
			_previous_frame = _database->get_frame_at_time(_previous_animation, (float)_previous_time);
		}
		if (_blend_weight >= 1.0f) {
			_previous_clip = StringName();
			_previous_frame = -1;
			_previous_animation = -1;
		}
	}

	Ref<MMAnimationEntry> entry = _database->get_animation_entry(_current_animation);
	if (entry.is_null()) {
		return;
	}

	const double length = entry->get_length();
	if (length > 0.0 && _current_time >= length) {
		if (entry->get_loop()) {
			_current_time = Math::fposmod(_current_time, length);
		} else {
			_current_time = length;
			// A one shot clip that ran out must not hold the last pose.
			_force_search = true;
		}
	}

	_current_frame = _database->get_frame_at_time(_current_animation, (float)_current_time);
}

bool MotionMatchingController::take_seek_request() {
	const bool requested = _seek_request;
	_seek_request = false;
	return requested;
}

Transform3D MotionMatchingController::consume_root_motion() {
	Transform3D delta = _root_motion->consume_delta();
	if (_motion_warp.is_valid() && _motion_warp->is_active() && _character != nullptr) {
		delta = _motion_warp->warp_delta(delta, _character->get_global_transform(),
				(float)_current_time, _last_delta_time);
	}
	// Cached purely for get_debug_info()'s root-motion-vs-actual-movement
	// comparison; does not affect what's returned to the caller.
	_last_root_motion_delta = delta.origin;
	return delta;
}

bool MotionMatchingController::get_current_left_foot_contact() const {
	if (_database.is_null() || _current_frame < 0 || _current_frame >= _database->get_frame_count()) {
		return false;
	}
	return (_database->get_frame_contacts_value(_current_frame) & 1) != 0;
}

bool MotionMatchingController::get_current_right_foot_contact() const {
	if (_database.is_null() || _current_frame < 0 || _current_frame >= _database->get_frame_count()) {
		return false;
	}
	return (_database->get_frame_contacts_value(_current_frame) & 2) != 0;
}

Dictionary MotionMatchingController::get_debug_info() const {
	Dictionary info;
	info["clip"] = _current_clip;
	info["time"] = _current_time;
	info["frame"] = _current_frame;
	info["animation_id"] = _current_animation;
	info["previous_clip"] = _previous_clip;
	info["blend_weight"] = _blend_weight;
	info["match_cost"] = _last_result.cost;
	info["continuation_cost"] = _continuation_cost;
	info["frames_compared"] = _stats.frames_compared;
	info["candidates_visited"] = _stats.candidates_visited;
	info["nodes_visited"] = _stats.nodes_visited;
	info["search_time_usec"] = _stats.search_time_usec;
	info["budget_exceeded"] = _stats.budget_exceeded;
	info["cache_hits"] = _cache.get_hits();
	info["cache_misses"] = _cache.get_misses();
	info["grounded"] = _grounded;
	info["time_in_clip"] = _time_in_clip;
	info["database_frames"] = _database.is_valid() ? _database->get_frame_count() : 0;

	// Per-frame foot contact, read from the same data the feature extractor
	// wrote during database build (bit 0 = left, bit 1 = right). This is what
	// MMFootIKModifier's contact-locking actually needs; without it the flags
	// silently default to false regardless of the currently playing frame.
	info["left_foot_contact"] = get_current_left_foot_contact();
	info["right_foot_contact"] = get_current_right_foot_contact();

	// Search-phase profiler report (frames_compared/candidates/nodes are
	// covered above per-tick already; this is the ring-buffer summary —
	// percentiles, worst case, switch count, budget overruns).
	info["profiler"] = _profiler->get_report();

	// Phase timings that don't belong in MMProfiler's search-shaped Sample.
	info["query_build_usec"] = _last_query_build_usec;
	info["continuation_eval_usec"] = _last_continuation_usec;
	info["switch_apply_usec"] = _last_switch_usec;
	info["update_total_usec"] = _last_update_usec;

	// Optional-subsystem state, present even when unassigned so a debug panel
	// can distinguish "not enabled" from "enabled but idle".
	info["traversal_active"] = _traversal.is_valid();
	info["traversal_type"] = _traversal.is_valid() ? (int)_traversal->get_detected_type() : 0;
	info["warp_active"] = is_warping();

	// Movement intent + acceleration debug: raw current/desired state from
	// the trajectory predictor, decomposed into the character's own
	// forward/right axes so "running forward" reads as forward regardless
	// of world-space heading.
	if (_trajectory.is_valid()) {
		const Vector3 current_velocity = _trajectory->get_current_velocity();
		const Vector3 desired_velocity = _trajectory->get_desired_velocity();
		const Vector3 acceleration = _trajectory->get_current_acceleration();
		const float current_speed = current_velocity.length();
		const float desired_speed = desired_velocity.length();
		const float max_speed = MAX(_trajectory->get_max_speed(), 0.0001f);

		info["speed_current"] = current_speed;
		info["speed_desired"] = desired_speed;
		info["speed_max"] = _trajectory->get_max_speed();
		info["acceleration"] = acceleration.length();

		Vector3 forward = _trajectory->get_current_facing();
		forward.y = 0.0f;
		if (forward.length_squared() > 0.000001f) {
			forward.normalize();
		} else {
			forward = Vector3(0, 0, -1);
		}
		const Vector3 right = forward.cross(Vector3(0, 1, 0)).normalized();

		if (desired_speed > 0.0001f) {
			const Vector3 desired_direction = desired_velocity / desired_speed;
			const float scale = MIN(desired_speed / max_speed, 1.0f) * 100.0f;
			info["intent_forward_percent"] = desired_direction.dot(forward) * scale;
			info["intent_right_percent"] = desired_direction.dot(right) * scale;
		} else {
			info["intent_forward_percent"] = 0.0f;
			info["intent_right_percent"] = 0.0f;
		}
		info["intent_stop_percent"] = CLAMP(100.0f - (desired_speed / max_speed) * 100.0f, 0.0f, 100.0f);

		// Turn-in-place: only meaningful while nearly stationary -- once the
		// character is moving, a facing difference is an ordinary turn, not
		// something that needs a dedicated idle-turn animation.
		const Vector3 desired_facing = _trajectory->get_desired_facing();
		const float facing_dot = CLAMP(forward.dot(desired_facing.normalized()), -1.0f, 1.0f);
		float turn_degrees = Math::rad_to_deg(Math::acos(facing_dot));
		if (forward.cross(desired_facing).y < 0.0f) {
			turn_degrees = -turn_degrees;
		}
		info["turn_intent_degrees"] = turn_degrees;
		// Classifies the continuous angle above into the 45/90/135/180
		// buckets a traditional turn-in-place clip set is usually authored
		// around -- see mm_turn_bucket_for_degrees()'s comment for why this
		// isn't a stored tag. Gameplay code can use this to bias which
		// pre-tagged clip group it looks for; the continuous yaw-rate
		// feature (schema-level, see MMFeatureSchema::get_yaw_rate_offset())
		// is what actually drives the search itself.
		info["turn_bucket"] = (int)mm_turn_bucket_for_degrees(turn_degrees);
		const float turn_magnitude = turn_degrees < 0.0f ? -turn_degrees : turn_degrees;
		info["turn_in_place_needed"] = current_speed < 0.15f && turn_magnitude > 30.0f;
	}

	// Root motion vs. actual character movement -- see the field comments on
	// _last_root_motion_delta/_last_actual_movement_delta (near their
	// declaration) for the one-tick offset this comparison carries.
	info["root_motion_delta_length"] = _last_root_motion_delta.length();
	info["actual_movement_delta_length"] = _last_actual_movement_delta.length();

	return info;
}

PackedVector3Array MotionMatchingController::get_debug_trajectory() const {
	return _trajectory->get_debug_points();
}

Array MotionMatchingController::get_debug_candidates(int p_count) const {
	if (_search.is_null() || !_search->is_built() || _cost_function.is_null() || _query.is_empty()) {
		return Array();
	}
	return _search->search_top_candidates_debug_raw(_query.ptr(), _cost_function, _build_filter(), p_count);
}

Dictionary MotionMatchingController::get_debug_cost_breakdown() const {
	Dictionary breakdown;
	if (_database.is_null() || _cost_function.is_null() || _query.is_empty() ||
			_last_result.frame_index < 0 || _last_result.frame_index >= _database->get_frame_count()) {
		return breakdown;
	}

	const int dimension = _query.size();
	const float *frame_features = _database->get_frame_features(_last_result.frame_index);

	// compute_group_errors() takes PackedFloat32Array (it is the scriptable,
	// debug-facing side of the cost function); the hot path's raw float*
	// query and frame data get copied into one only here, off the hot path.
	PackedFloat32Array query_vector;
	query_vector.resize(dimension);
	PackedFloat32Array frame_vector;
	frame_vector.resize(dimension);
	for (int i = 0; i < dimension; i++) {
		query_vector.set(i, _query[i]);
		frame_vector.set(i, frame_features[i]);
	}

	const PackedFloat32Array errors = _cost_function->compute_group_errors(query_vector, frame_vector);

	static const char *group_names[MM_GROUP_MAX] = {
		"trajectory_position",
		"trajectory_direction",
		"pose_position",
		"pose_velocity",
		"root_velocity",
		"extra",
		"yaw_rate",
	};

	float total = 0.0f;
	for (int i = 0; i < errors.size(); i++) {
		total += errors[i];
	}

	for (int i = 0; i < errors.size() && i < MM_GROUP_MAX; i++) {
		breakdown[group_names[i]] = total > 0.0001f ? (errors[i] / total) * 100.0f : 0.0f;
	}
	return breakdown;
}

Dictionary MotionMatchingController::get_debug_footstep_timing() const {
	Dictionary result;
	if (_database.is_null() || _current_animation < 0 || _current_frame < 0 ||
			_current_frame >= _database->get_frame_count()) {
		return result;
	}

	const uint8_t current_contacts = _database->get_frame_contacts_value(_current_frame);
	const int frame_count = _database->get_frame_count();
	// Generous but bounded look-ahead. Frames of one animation are stored
	// contiguously in build order, so hitting a different animation_id (or
	// the end of the database) means this clip's own frames ran out within
	// the horizon.
	const int horizon = 90;
	int next_left_frame = -1;
	int next_right_frame = -1;
	for (int offset = 1; offset <= horizon; offset++) {
		const int frame = _current_frame + offset;
		if (frame >= frame_count || _database->get_frame_animation_id(frame) != _current_animation) {
			break;
		}
		const uint8_t contacts = _database->get_frame_contacts_value(frame);
		if (next_left_frame < 0 && (contacts & 1) != (current_contacts & 1)) {
			next_left_frame = frame;
		}
		if (next_right_frame < 0 && (contacts & 2) != (current_contacts & 2)) {
			next_right_frame = frame;
		}
		if (next_left_frame >= 0 && next_right_frame >= 0) {
			break;
		}
	}

	const float now_time = _database->get_frame_time_value(_current_frame);
	if (next_left_frame >= 0) {
		result["next_left_change_seconds"] = _database->get_frame_time_value(next_left_frame) - now_time;
	}
	if (next_right_frame >= 0) {
		result["next_right_change_seconds"] = _database->get_frame_time_value(next_right_frame) - now_time;
	}
	return result;
}

Dictionary MotionMatchingController::get_debug_filter_stats() const {
	if (_search.is_null() || !_search->is_built()) {
		return Dictionary();
	}
	return _search->count_filtered_frames_debug_raw(_build_filter());
}

void MotionMatchingController::_record_search_cost(int p_animation_id, float p_cost) {
	if (p_animation_id < 0 || !Math::is_finite((double)p_cost)) {
		return;
	}

	Dictionary entry = _animation_stats.get(p_animation_id, Dictionary());
	if (entry.is_empty()) {
		Ref<MMAnimationEntry> anim = _database.is_valid() ? _database->get_animation_entry(p_animation_id) : Ref<MMAnimationEntry>();
		entry["clip_name"] = anim.is_valid() ? String(anim->get_qualified_name()) : vformat("id %d", p_animation_id);
		entry["search_count"] = 0;
		entry["total_cost"] = 0.0f;
		entry["play_count"] = 0;
	}
	entry["search_count"] = (int)entry["search_count"] + 1;
	entry["total_cost"] = (float)entry["total_cost"] + p_cost;
	_animation_stats[p_animation_id] = entry;
}

void MotionMatchingController::_record_transition(const String &p_from_clip, const String &p_to_clip, float p_cost) {
	Dictionary log_entry;
	log_entry["time"] = (double)Time::get_singleton()->get_ticks_msec() / 1000.0;
	log_entry["from_clip"] = p_from_clip;
	log_entry["to_clip"] = p_to_clip;
	log_entry["cost"] = p_cost;
	_transition_log.push_back(log_entry);

	static const int MM_TRANSITION_LOG_CAPACITY = 24;
	while (_transition_log.size() > MM_TRANSITION_LOG_CAPACITY) {
		_transition_log.remove_at(0);
	}

	if (_current_animation < 0) {
		return;
	}
	Dictionary entry = _animation_stats.get(_current_animation, Dictionary());
	if (entry.is_empty()) {
		entry["clip_name"] = p_to_clip;
		entry["search_count"] = 0;
		entry["total_cost"] = 0.0f;
		entry["play_count"] = 0;
	}
	entry["play_count"] = (int)entry["play_count"] + 1;
	_animation_stats[_current_animation] = entry;
}

Dictionary MotionMatchingController::get_debug_analytics() const {
	Dictionary result;
	Array keys = _animation_stats.keys();
	for (int i = 0; i < keys.size(); i++) {
		const Variant key = keys[i];
		const Dictionary source = _animation_stats[key];
		Dictionary entry;
		entry["clip_name"] = source.get("clip_name", "");
		entry["search_count"] = source.get("search_count", 0);
		entry["play_count"] = source.get("play_count", 0);
		const int search_count = (int)source.get("search_count", 0);
		const float total_cost = (float)source.get("total_cost", 0.0f);
		entry["average_cost"] = search_count > 0 ? total_cost / (float)search_count : 0.0f;
		result[key] = entry;
	}
	return result;
}

Array MotionMatchingController::get_debug_transitions() const {
	return _transition_log.duplicate();
}

void MotionMatchingController::reset_debug_analytics() {
	_animation_stats.clear();
	_transition_log.clear();
}

void MotionMatchingController::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resource", "resource"), &MotionMatchingController::set_resource);
	ClassDB::bind_method(D_METHOD("get_resource"), &MotionMatchingController::get_resource);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "resource", PROPERTY_HINT_RESOURCE_TYPE,
						  "MotionMatchingResource"),
			"set_resource", "get_resource");

	ClassDB::bind_method(D_METHOD("set_character_path", "path"), &MotionMatchingController::set_character_path);
	ClassDB::bind_method(D_METHOD("get_character_path"), &MotionMatchingController::get_character_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "character_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES,
						  "Node3D"),
			"set_character_path", "get_character_path");

	ClassDB::bind_method(D_METHOD("set_animation_tree_path", "path"),
			&MotionMatchingController::set_animation_tree_path);
	ClassDB::bind_method(D_METHOD("get_animation_tree_path"),
			&MotionMatchingController::get_animation_tree_path);
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_tree_path",
						  PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AnimationTree"),
			"set_animation_tree_path", "get_animation_tree_path");

	MM_BIND_PROPERTY(MotionMatchingController, Variant::BOOL, async_search)
	MM_BIND_PROPERTY(MotionMatchingController, Variant::BOOL, auto_update)

	ClassDB::bind_method(D_METHOD("set_velocity", "velocity"), &MotionMatchingController::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &MotionMatchingController::get_velocity);
	ClassDB::bind_method(D_METHOD("set_desired_velocity", "velocity"),
			&MotionMatchingController::set_desired_velocity);
	ClassDB::bind_method(D_METHOD("set_direction", "direction"), &MotionMatchingController::set_direction);
	ClassDB::bind_method(D_METHOD("set_facing", "facing"), &MotionMatchingController::set_facing);
	ClassDB::bind_method(D_METHOD("set_ground_state", "grounded"), &MotionMatchingController::set_ground_state);
	ClassDB::bind_method(D_METHOD("get_ground_state"), &MotionMatchingController::get_ground_state);
	ClassDB::bind_method(D_METHOD("set_fall_distance", "distance"), &MotionMatchingController::set_fall_distance);
	ClassDB::bind_method(D_METHOD("request_jump"), &MotionMatchingController::request_jump);
	ClassDB::bind_method(D_METHOD("set_trajectory", "positions", "directions"),
			&MotionMatchingController::set_trajectory);
	ClassDB::bind_method(D_METHOD("get_trajectory"), &MotionMatchingController::get_trajectory);

	ClassDB::bind_method(D_METHOD("set_required_tags", "tags"), &MotionMatchingController::set_required_tags);
	ClassDB::bind_method(D_METHOD("get_required_tags"), &MotionMatchingController::get_required_tags);
	ClassDB::bind_method(D_METHOD("set_blocked_tags", "tags"), &MotionMatchingController::set_blocked_tags);
	ClassDB::bind_method(D_METHOD("get_blocked_tags"), &MotionMatchingController::get_blocked_tags);
	ClassDB::bind_method(D_METHOD("set_category_mask", "mask"), &MotionMatchingController::set_category_mask);
	ClassDB::bind_method(D_METHOD("get_category_mask"), &MotionMatchingController::get_category_mask);
	ClassDB::bind_method(D_METHOD("lock_category", "category", "duration"),
			&MotionMatchingController::lock_category);
	ClassDB::bind_method(D_METHOD("release_category"), &MotionMatchingController::release_category);

	ClassDB::bind_method(D_METHOD("update", "delta"), &MotionMatchingController::update);
	ClassDB::bind_method(D_METHOD("force_search"), &MotionMatchingController::force_search);
	ClassDB::bind_method(D_METHOD("rebuild"), &MotionMatchingController::rebuild);

	ClassDB::bind_method(D_METHOD("get_current_clip"), &MotionMatchingController::get_current_clip);
	ClassDB::bind_method(D_METHOD("get_current_time"), &MotionMatchingController::get_current_time);
	ClassDB::bind_method(D_METHOD("get_previous_clip"), &MotionMatchingController::get_previous_clip);
	ClassDB::bind_method(D_METHOD("get_previous_time"), &MotionMatchingController::get_previous_time);
	ClassDB::bind_method(D_METHOD("get_blend_weight"), &MotionMatchingController::get_blend_weight);
	ClassDB::bind_method(D_METHOD("get_current_frame"), &MotionMatchingController::get_current_frame);
	ClassDB::bind_method(D_METHOD("get_current_animation_id"),
			&MotionMatchingController::get_current_animation_id);
	ClassDB::bind_method(D_METHOD("get_root_motion"), &MotionMatchingController::get_root_motion);
	ClassDB::bind_method(D_METHOD("consume_root_motion"), &MotionMatchingController::consume_root_motion);
	ClassDB::bind_method(D_METHOD("get_debug_info"), &MotionMatchingController::get_debug_info);
	ClassDB::bind_method(D_METHOD("get_current_left_foot_contact"),
			&MotionMatchingController::get_current_left_foot_contact);
	ClassDB::bind_method(D_METHOD("get_current_right_foot_contact"),
			&MotionMatchingController::get_current_right_foot_contact);
	ClassDB::bind_method(D_METHOD("get_debug_trajectory"), &MotionMatchingController::get_debug_trajectory);
	ClassDB::bind_method(D_METHOD("get_debug_candidates", "count"),
			&MotionMatchingController::get_debug_candidates, DEFVAL(3));
	ClassDB::bind_method(D_METHOD("get_debug_cost_breakdown"),
			&MotionMatchingController::get_debug_cost_breakdown);
	ClassDB::bind_method(D_METHOD("get_debug_footstep_timing"),
			&MotionMatchingController::get_debug_footstep_timing);
	ClassDB::bind_method(D_METHOD("get_debug_filter_stats"),
			&MotionMatchingController::get_debug_filter_stats);
	ClassDB::bind_method(D_METHOD("get_debug_analytics"),
			&MotionMatchingController::get_debug_analytics);
	ClassDB::bind_method(D_METHOD("get_debug_transitions"),
			&MotionMatchingController::get_debug_transitions);
	ClassDB::bind_method(D_METHOD("reset_debug_analytics"),
			&MotionMatchingController::reset_debug_analytics);

	ClassDB::bind_method(D_METHOD("get_profiler"), &MotionMatchingController::get_profiler);

	ClassDB::bind_method(D_METHOD("set_traversal", "traversal"), &MotionMatchingController::set_traversal);
	ClassDB::bind_method(D_METHOD("get_traversal"), &MotionMatchingController::get_traversal);

	ClassDB::bind_method(D_METHOD("set_motion_warp", "warp"), &MotionMatchingController::set_motion_warp);
	ClassDB::bind_method(D_METHOD("get_motion_warp"), &MotionMatchingController::get_motion_warp);
	ClassDB::bind_method(D_METHOD("begin_warp", "target"), &MotionMatchingController::begin_warp);
	ClassDB::bind_method(D_METHOD("end_warp"), &MotionMatchingController::end_warp);
	ClassDB::bind_method(D_METHOD("is_warping"), &MotionMatchingController::is_warping);

	ADD_SIGNAL(MethodInfo("animation_changed", PropertyInfo(Variant::STRING_NAME, "clip"),
			PropertyInfo(Variant::FLOAT, "time"), PropertyInfo(Variant::FLOAT, "cost")));
	ADD_SIGNAL(MethodInfo("search_completed", PropertyInfo(Variant::DICTIONARY, "debug_info")));
	ADD_SIGNAL(MethodInfo("traversal_requested", PropertyInfo(Variant::INT, "traversal_type"),
			PropertyInfo(Variant::VECTOR3, "target")));
}
