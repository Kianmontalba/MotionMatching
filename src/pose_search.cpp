#include "pose_search.hpp"

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------

void MMPoseSearch::set_leaf_size(int p_size) {
	_leaf_size = MAX(4, p_size);
}

void MMPoseSearch::clear() {
	_nodes.clear();
	_indices.clear();
	_built = false;
}

void MMPoseSearch::build(const Ref<MotionMatchingDatabase> &p_database) {
	clear();
	ERR_FAIL_COND(p_database.is_null());

	_database = p_database;
	_dimension = p_database->get_dimension();
	const int frames = p_database->get_frame_count();
	if (frames <= 0 || _dimension <= 0) {
		return;
	}

	_indices.resize(frames);
	for (int i = 0; i < frames; i++) {
		_indices.write[i] = i;
	}

	_build_recursive(0, frames, 0);
	_built = true;
}

// Splits on the widest axis rather than cycling through dimensions. The widest
// axis is estimated from a bounded sample, so build time stays linear even for
// a database with a million frames.
int MMPoseSearch::_build_recursive(int p_begin, int p_end, int p_depth) {
	const int node_index = _nodes.size();
	_nodes.push_back(KDNode());

	const int count = p_end - p_begin;
	if (count <= _leaf_size || p_depth > 48) {
		KDNode leaf;
		leaf.split_dimension = -1;
		leaf.begin = p_begin;
		leaf.end = p_end;
		_nodes.write[node_index] = leaf;
		return node_index;
	}

	const int sample_step = MAX(1, count / 256);
	int split_dimension = 0;
	float widest = -1.0f;

	for (int d = 0; d < _dimension; d++) {
		float minimum = MM_INFINITY;
		float maximum = -MM_INFINITY;
		for (int i = p_begin; i < p_end; i += sample_step) {
			const float value = _database->get_frame_features(_indices[i])[d];
			minimum = MIN(minimum, value);
			maximum = MAX(maximum, value);
		}
		const float spread = maximum - minimum;
		if (spread > widest) {
			widest = spread;
			split_dimension = d;
		}
	}

	// Quickselect the median in place. Only the index array moves; the feature
	// block is never reordered, so frame indices stay meaningful everywhere
	// else in the framework.
	const int median = p_begin + count / 2;
	int left = p_begin;
	int right = p_end - 1;

	while (left < right) {
		const float pivot = _database->get_frame_features(_indices[(left + right) / 2])[split_dimension];
		int i = left;
		int j = right;
		while (i <= j) {
			while (_database->get_frame_features(_indices[i])[split_dimension] < pivot) {
				i++;
			}
			while (_database->get_frame_features(_indices[j])[split_dimension] > pivot) {
				j--;
			}
			if (i <= j) {
				const int swap = _indices[i];
				_indices.write[i] = _indices[j];
				_indices.write[j] = swap;
				i++;
				j--;
			}
		}
		if (median <= j) {
			right = j;
		} else if (median >= i) {
			left = i;
		} else {
			break;
		}
	}

	KDNode node;
	node.split_dimension = split_dimension;
	node.split_value = _database->get_frame_features(_indices[median])[split_dimension];
	node.begin = p_begin;
	node.end = p_end;
	node.left = _build_recursive(p_begin, median, p_depth + 1);
	node.right = _build_recursive(median, p_end, p_depth + 1);
	_nodes.write[node_index] = node;
	return node_index;
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

_FORCE_INLINE_ void MMPoseSearch::_evaluate_frame(int p_frame, SearchState &r_state) const {
	const MotionMatchingDatabase *db = r_state.db;

	if (!r_state.filter->accepts(db->get_frame_tag_mask(p_frame),
				db->get_frame_category_id(p_frame), db->get_frame_speed_value(p_frame))) {
		return;
	}

	r_state.stats->candidates_visited++;

	const float cost = r_state.cost->compute_raw(r_state.query, db->get_frame_features(p_frame),
			r_state.weights, r_state.dimension, r_state.best_cost);

	r_state.stats->frames_compared++;

	if (cost < r_state.best_cost) {
		r_state.best_cost = cost;
		r_state.best_frame = p_frame;
	}
}

void MMPoseSearch::_evaluate_range(int p_begin, int p_end, SearchState &r_state) const {
	for (int i = p_begin; i < p_end; i++) {
		_evaluate_frame(_indices[i], r_state);
	}
}

void MMPoseSearch::_descend(int p_node, SearchState &r_state) const {
	if (p_node < 0) {
		return;
	}

	if (r_state.max_comparisons > 0 && r_state.stats->frames_compared >= r_state.max_comparisons) {
		r_state.stats->budget_exceeded = true;
		return;
	}

	const KDNode &node = _nodes[p_node];
	r_state.stats->nodes_visited++;

	if (node.split_dimension < 0) {
		_evaluate_range(node.begin, node.end, r_state);
		return;
	}

	const float delta = r_state.query[node.split_dimension] - node.split_value;
	const int near_child = delta <= 0.0f ? node.left : node.right;
	const int far_child = delta <= 0.0f ? node.right : node.left;

	_descend(near_child, r_state);

	// The far side can only contain a better frame if crossing the split plane
	// alone does not already cost more than the best match so far.
	const float bound = r_state.weights[node.split_dimension] * delta * delta;
	if (bound < r_state.best_cost) {
		_descend(far_child, r_state);
	}
}

MMMatchResult MMPoseSearch::search(const float *p_query, const Ref<MMCostFunction> &p_cost,
		const MMSearchFilter &p_filter, const MMSearchContext &p_context,
		MMSearchStats &r_stats) const {
	MMMatchResult result;
	if (!_built || _database.is_null() || p_cost.is_null()) {
		return result;
	}

	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	r_stats.reset();

	SearchState state;
	state.query = p_query;
	state.weights = p_cost->get_dimension_weights();
	state.filter = &p_filter;
	state.db = _database.ptr();
	state.cost = p_cost.ptr();
	state.dimension = _dimension;
	state.max_comparisons = p_context.max_comparisons;
	state.best_cost = p_context.best_cost_seed;
	state.stats = &r_stats;

	// Temporal coherence. Motion is continuous, so the answer is usually a
	// neighbour of last tick's answer. Evaluating that neighbourhood first
	// produces a tight upper bound, and a tight bound is what lets the tree
	// prune away entire branches instead of walking them.
	const int frames = _database->get_frame_count();
	const int radius = MAX(0, p_context.neighborhood_radius);

	if (radius > 0) {
		const int hints[2] = { p_context.continuation_frame, p_context.previous_frame };
		for (int h = 0; h < 2; h++) {
			const int center = hints[h];
			if (center < 0 || center >= frames) {
				continue;
			}
			const int animation_id = _database->get_frame_animation_id(center);
			Ref<MMAnimationEntry> entry = _database->get_animation_entry(animation_id);
			int begin = center - radius;
			int end = center + radius;
			if (entry.is_valid()) {
				begin = MAX(begin, entry->get_frame_start());
				end = MIN(end, entry->get_frame_start() + entry->get_frame_count() - 1);
			}
			begin = MAX(begin, 0);
			end = MIN(end, frames - 1);
			for (int f = begin; f <= end; f++) {
				_evaluate_frame(f, state);
			}
		}
	}

	_descend(0, state);

	if (state.best_frame >= 0) {
		result.frame_index = state.best_frame;
		result.animation_id = _database->get_frame_animation_id(state.best_frame);
		result.animation_time = _database->get_frame_time_value(state.best_frame);
		result.normalized_time = _database->get_frame_normalized_value(state.best_frame);
		result.cost = state.best_cost;
	}

	r_stats.search_time_usec = (double)(Time::get_singleton()->get_ticks_usec() - started);
	return result;
}

MMMatchResult MMPoseSearch::search_brute_force(const float *p_query, const Ref<MMCostFunction> &p_cost,
		const MMSearchFilter &p_filter, MMSearchStats &r_stats) const {
	MMMatchResult result;
	if (_database.is_null() || p_cost.is_null()) {
		return result;
	}

	const uint64_t started = Time::get_singleton()->get_ticks_usec();
	r_stats.reset();

	SearchState state;
	state.query = p_query;
	state.weights = p_cost->get_dimension_weights();
	state.filter = &p_filter;
	state.db = _database.ptr();
	state.cost = p_cost.ptr();
	state.dimension = _dimension;
	state.stats = &r_stats;

	const int frames = _database->get_frame_count();
	for (int f = 0; f < frames; f++) {
		_evaluate_frame(f, state);
	}

	if (state.best_frame >= 0) {
		result.frame_index = state.best_frame;
		result.animation_id = _database->get_frame_animation_id(state.best_frame);
		result.animation_time = _database->get_frame_time_value(state.best_frame);
		result.normalized_time = _database->get_frame_normalized_value(state.best_frame);
		result.cost = state.best_cost;
	}

	r_stats.search_time_usec = (double)(Time::get_singleton()->get_ticks_usec() - started);
	return result;
}

Dictionary MMPoseSearch::search_query(const PackedFloat32Array &p_query, const Ref<MMCostFunction> &p_cost,
		int p_required_tags, int p_blocked_tags, int p_category_mask) {
	Dictionary output;
	if (p_query.size() < _dimension) {
		return output;
	}

	MMSearchFilter filter;
	filter.required_tags = (uint32_t)p_required_tags;
	filter.blocked_tags = (uint32_t)p_blocked_tags;
	filter.category_mask = (uint32_t)p_category_mask;

	MMSearchContext context;
	MMSearchStats stats;
	const MMMatchResult result = search(p_query.ptr(), p_cost, filter, context, stats);

	output["frame_index"] = result.frame_index;
	output["animation_id"] = result.animation_id;
	output["animation_time"] = result.animation_time;
	output["normalized_time"] = result.normalized_time;
	output["cost"] = result.cost;
	output["frames_compared"] = stats.frames_compared;
	output["candidates_visited"] = stats.candidates_visited;
	output["nodes_visited"] = stats.nodes_visited;
	output["search_time_usec"] = stats.search_time_usec;
	return output;
}

Dictionary MMPoseSearch::search_brute_force_query(const PackedFloat32Array &p_query,
		const Ref<MMCostFunction> &p_cost, int p_required_tags, int p_blocked_tags,
		int p_category_mask) {
	Dictionary output;
	if (p_query.size() < _dimension) {
		return output;
	}

	MMSearchFilter filter;
	filter.required_tags = (uint32_t)p_required_tags;
	filter.blocked_tags = (uint32_t)p_blocked_tags;
	filter.category_mask = (uint32_t)p_category_mask;

	MMSearchStats stats;
	const MMMatchResult result = search_brute_force(p_query.ptr(), p_cost, filter, stats);

	output["frame_index"] = result.frame_index;
	output["animation_id"] = result.animation_id;
	output["animation_time"] = result.animation_time;
	output["normalized_time"] = result.normalized_time;
	output["cost"] = result.cost;
	output["frames_compared"] = stats.frames_compared;
	output["search_time_usec"] = stats.search_time_usec;
	return output;
}

Array MMPoseSearch::search_top_candidates_debug_raw(const float *p_query, const Ref<MMCostFunction> &p_cost,
		const MMSearchFilter &p_filter, int p_count) const {
	Array results;
	if (_database.is_null() || p_cost.is_null() || p_count <= 0 || p_query == nullptr) {
		return results;
	}

	const float *weights = p_cost->get_dimension_weights();
	const int frame_count = _database->get_frame_count();

	// Small sorted top-K kept as parallel arrays, worst-to-best insertion.
	// p_count is expected to be a handful (3-5), so a linear insertion costs
	// nothing next to the per-frame cost computation it rides alongside.
	Vector<int> top_frames;
	Vector<float> top_costs;

	for (int frame = 0; frame < frame_count; frame++) {
		if (!p_filter.accepts(_database->get_frame_tag_mask(frame),
					_database->get_frame_category_id(frame), _database->get_frame_speed_value(frame))) {
			continue;
		}

		// No early-out here: a cost abandoned mid-computation by the fast
		// path would misrepresent a near-miss as worse than it really is,
		// and this list exists specifically to show accurate near-misses.
		const float cost = p_cost->compute_raw(p_query, _database->get_frame_features(frame),
				weights, _dimension, MM_INFINITY);

		int insert_at = top_frames.size();
		for (int i = 0; i < top_frames.size(); i++) {
			if (cost < top_costs[i]) {
				insert_at = i;
				break;
			}
		}
		if (insert_at >= p_count) {
			continue;
		}
		top_frames.insert(insert_at, frame);
		top_costs.insert(insert_at, cost);
		if (top_frames.size() > p_count) {
			top_frames.remove_at(top_frames.size() - 1);
			top_costs.remove_at(top_costs.size() - 1);
		}
	}

	for (int i = 0; i < top_frames.size(); i++) {
		Dictionary entry = _database->get_frame_info(top_frames[i]);
		entry["cost"] = top_costs[i];
		results.push_back(entry);
	}
	return results;
}

Array MMPoseSearch::search_top_candidates_debug(const PackedFloat32Array &p_query, const Ref<MMCostFunction> &p_cost,
		int p_required_tags, int p_blocked_tags, int p_category_mask, int p_count) const {
	if (p_query.size() < _dimension) {
		return Array();
	}

	MMSearchFilter filter;
	filter.required_tags = (uint32_t)p_required_tags;
	filter.blocked_tags = (uint32_t)p_blocked_tags;
	filter.category_mask = (uint32_t)p_category_mask;

	return search_top_candidates_debug_raw(p_query.ptr(), p_cost, filter, p_count);
}

void MMPoseSearch::_bind_methods() {
	ClassDB::bind_method(D_METHOD("build", "database"), &MMPoseSearch::build);
	ClassDB::bind_method(D_METHOD("clear"), &MMPoseSearch::clear);
	ClassDB::bind_method(D_METHOD("is_built"), &MMPoseSearch::is_built);
	ClassDB::bind_method(D_METHOD("get_node_count"), &MMPoseSearch::get_node_count);
	ClassDB::bind_method(D_METHOD("set_leaf_size", "size"), &MMPoseSearch::set_leaf_size);
	ClassDB::bind_method(D_METHOD("get_leaf_size"), &MMPoseSearch::get_leaf_size);
	ClassDB::bind_method(D_METHOD("search_query", "query", "cost", "required_tags", "blocked_tags",
								 "category_mask"),
			&MMPoseSearch::search_query);
	ClassDB::bind_method(D_METHOD("search_brute_force_query", "query", "cost", "required_tags",
								 "blocked_tags", "category_mask"),
			&MMPoseSearch::search_brute_force_query);
	ClassDB::bind_method(D_METHOD("search_top_candidates_debug", "query", "cost", "required_tags",
								 "blocked_tags", "category_mask", "count"),
			&MMPoseSearch::search_top_candidates_debug);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "leaf_size"), "set_leaf_size", "get_leaf_size");
}
