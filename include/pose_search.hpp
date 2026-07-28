#ifndef MM_POSE_SEARCH_HPP
#define MM_POSE_SEARCH_HPP

#include "cost_function.hpp"
#include "frame_database.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMPoseSearch
//
// The multi stage search described in Part 5, in one object:
//
//   stage 1-4  gameplay, category, tag and speed filters (MMSearchFilter)
//   stage 5    temporal coherence pass over the neighborhood of the frames we
//              already care about, which seeds a tight upper bound
//   stage 6    KD tree descent with weighted pruning
//   stage 7    exact weighted cost on the surviving candidates
//
// The tree is built over the normalized feature vectors of the database. It
// stores frame indices, never copies of the features, so a 10 000 clip
// database costs roughly 8 bytes per frame on top of the feature block.
// ---------------------------------------------------------------------------
class MMPoseSearch : public RefCounted {
	GDCLASS(MMPoseSearch, RefCounted);

private:
	struct KDNode {
		int split_dimension = -1; // -1 marks a leaf.
		float split_value = 0.0f;
		int left = -1;
		int right = -1;
		int begin = 0; // Range into _indices, leaves only.
		int end = 0;
	};

	Ref<MotionMatchingDatabase> _database;
	Vector<KDNode> _nodes;
	Vector<int> _indices;
	int _leaf_size = 24;
	int _dimension = 0;
	bool _built = false;

	// Scratch state for a single search. Kept as members to avoid touching the
	// allocator inside the hot path.
	struct SearchState {
		const float *query = nullptr;
		const float *weights = nullptr;
		const MMSearchFilter *filter = nullptr;
		const MotionMatchingDatabase *db = nullptr;
		const MMCostFunction *cost = nullptr;
		int dimension = 0;
		int max_comparisons = 0;
		float best_cost = MM_INFINITY;
		int best_frame = -1;
		MMSearchStats *stats = nullptr;
	};

	int _build_recursive(int p_begin, int p_end, int p_depth);
	void _descend(int p_node, SearchState &r_state) const;
	void _evaluate_range(int p_begin, int p_end, SearchState &r_state) const;
	_FORCE_INLINE_ void _evaluate_frame(int p_frame, SearchState &r_state) const;

protected:
	static void _bind_methods();

public:
	void build(const Ref<MotionMatchingDatabase> &p_database);
	bool is_built() const { return _built; }
	void clear();

	int get_node_count() const { return _nodes.size(); }
	void set_leaf_size(int p_size);
	int get_leaf_size() const { return _leaf_size; }

	// Full search. Returns an invalid result only when every frame was
	// rejected by the filter. p_max_dimension caps how many leading feature
	// dimensions are actually compared -- pass a value from
	// MMFeatureSchema::get_lod_dimension() to run a cheaper, lower quality
	// search on weak hardware; -1 (default) uses every dimension the tree
	// was built with. See _descend()'s comment for why capping this is
	// safe even though the tree itself is still the full-dimension one.
	MMMatchResult search(const float *p_query, const Ref<MMCostFunction> &p_cost,
			const MMSearchFilter &p_filter, const MMSearchContext &p_context,
			MMSearchStats &r_stats, int p_max_dimension = -1) const;

	// Brute force reference implementation, used by the tests and by the
	// editor when validating that an acceleration structure is consistent.
	MMMatchResult search_brute_force(const float *p_query, const Ref<MMCostFunction> &p_cost,
			const MMSearchFilter &p_filter, MMSearchStats &r_stats) const;

	// Scriptable wrapper for tools and debugging.
	Dictionary search_query(const PackedFloat32Array &p_query, const Ref<MMCostFunction> &p_cost,
			int p_required_tags, int p_blocked_tags, int p_category_mask);

	// Scriptable wrapper for search_brute_force(), mirroring search_query()'s
	// shape exactly. Exists so the GDScript test suite can assert the tree
	// search agrees with the reference implementation on the same query.
	Dictionary search_brute_force_query(const PackedFloat32Array &p_query, const Ref<MMCostFunction> &p_cost,
			int p_required_tags, int p_blocked_tags, int p_category_mask);

	// Debug-only candidate list: the p_count best frames by exact cost, no
	// pruning. This is O(frame_count) per call -- a candidate-preview panel
	// polling a few times a second is fine, calling it every gameplay frame
	// is not. The runtime search() never calls this; it exists purely so a
	// debug panel can show why the winner beat the runners-up, not just what
	// the winner was.
	Array search_top_candidates_debug_raw(const float *p_query, const Ref<MMCostFunction> &p_cost,
			const MMSearchFilter &p_filter, int p_count) const;

	// Scriptable wrapper for search_top_candidates_debug_raw(), mirroring
	// search_query()'s tag/category encoding.
	Array search_top_candidates_debug(const PackedFloat32Array &p_query, const Ref<MMCostFunction> &p_cost,
			int p_required_tags, int p_blocked_tags, int p_category_mask, int p_count) const;

	// Debug-only: total frame count vs how many survive tag/category/speed
	// filtering, before any cost is computed at all. Answers "why did
	// filtering leave so few options" independently of the query itself.
	// Cheap per frame (no float math), but still O(frame_count) -- fine for
	// a debug panel, not the hot search path.
	Dictionary count_filtered_frames_debug_raw(const MMSearchFilter &p_filter) const;

	// Scriptable wrapper for count_filtered_frames_debug_raw().
	Dictionary count_filtered_frames_debug(int p_required_tags, int p_blocked_tags, int p_category_mask) const;
};

} // namespace godot

#endif // MM_POSE_SEARCH_HPP
