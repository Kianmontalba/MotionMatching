#ifndef MM_COST_FUNCTION_HPP
#define MM_COST_FUNCTION_HPP

#include "feature.hpp"
#include "mm_types.hpp"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMCostFunction
//
// Turns two normalized feature vectors into a single scalar cost. Weights are
// authored per feature group, then baked into a per dimension weight table so
// the inner loop is a plain weighted squared distance with no branching.
//
// The same weight table is handed to the KD tree, which needs it to prune
// correctly: a split can only be skipped when weight[dim] * delta^2 already
// exceeds the best cost found so far.
//
// Extend by inheriting in GDScript or C++ and overriding _compute_cost().
// ---------------------------------------------------------------------------
class MMCostFunction : public RefCounted {
	GDCLASS(MMCostFunction, RefCounted);

private:
	float _weight_trajectory_position = 1.0f;
	float _weight_trajectory_direction = 1.0f;
	float _weight_pose_position = 0.75f;
	float _weight_pose_velocity = 0.75f;
	float _weight_root_velocity = 1.0f;
	float _weight_extra = 1.0f;
	float _weight_yaw_rate = 1.0f;

	// Extra cost added to every frame that is not a continuation of the clip
	// currently playing. This is the hysteresis that keeps motion readable.
	float _switch_penalty = 0.0f;

	PackedFloat32Array _dimension_weights;
	Ref<MMFeatureSchema> _schema;
	bool _dirty = true;

protected:
	static void _bind_methods();

public:
	void set_schema(const Ref<MMFeatureSchema> &p_schema);
	Ref<MMFeatureSchema> get_schema() const { return _schema; }

	void set_group_weight(int p_group, float p_weight);
	float get_group_weight(int p_group) const;

	MM_ACCESSORS(float, weight_trajectory_position)
	MM_ACCESSORS(float, weight_trajectory_direction)
	MM_ACCESSORS(float, weight_pose_position)
	MM_ACCESSORS(float, weight_pose_velocity)
	MM_ACCESSORS(float, weight_root_velocity)
	MM_ACCESSORS(float, weight_extra)
	MM_ACCESSORS(float, weight_yaw_rate)
	MM_ACCESSORS(float, switch_penalty)

	// Rebuilds the per dimension weight table from the group weights.
	void rebuild(const Ref<MMFeatureSchema> &p_schema);
	void mark_dirty() { _dirty = true; }
	bool is_dirty() const { return _dirty; }

	const float *get_dimension_weights() const { return _dimension_weights.ptr(); }
	PackedFloat32Array get_dimension_weight_table() const { return _dimension_weights; }

	// Hot path. p_early_out lets the caller abandon a frame as soon as it can
	// no longer win, which typically skips 60 to 80 percent of the work.
	_FORCE_INLINE_ float compute_raw(const float *p_query, const float *p_frame,
			const float *p_weights, int p_dimension, float p_early_out) const {
		float cost = 0.0f;
		for (int i = 0; i < p_dimension; i++) {
			const float d = p_query[i] - p_frame[i];
			cost += p_weights[i] * d * d;
			if (cost >= p_early_out) {
				return MM_INFINITY;
			}
		}
		return cost;
	}

	// Scriptable entry point. Slower, meant for prototyping custom metrics.
	virtual float compute_cost(const PackedFloat32Array &p_query,
			const PackedFloat32Array &p_frame) const;

	// Per group breakdown for the debug panel.
	PackedFloat32Array compute_group_errors(const PackedFloat32Array &p_query,
			const PackedFloat32Array &p_frame) const;
};

} // namespace godot

#endif // MM_COST_FUNCTION_HPP
