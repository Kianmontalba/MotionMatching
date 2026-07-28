#include "cost_function.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void MMCostFunction::set_schema(const Ref<MMFeatureSchema> &p_schema) {
	_schema = p_schema;
	_dirty = true;
}

void MMCostFunction::set_group_weight(int p_group, float p_weight) {
	switch (p_group) {
		case MM_GROUP_TRAJECTORY_POSITION:
			_weight_trajectory_position = p_weight;
			break;
		case MM_GROUP_TRAJECTORY_DIRECTION:
			_weight_trajectory_direction = p_weight;
			break;
		case MM_GROUP_POSE_POSITION:
			_weight_pose_position = p_weight;
			break;
		case MM_GROUP_POSE_VELOCITY:
			_weight_pose_velocity = p_weight;
			break;
		case MM_GROUP_ROOT_VELOCITY:
			_weight_root_velocity = p_weight;
			break;
		case MM_GROUP_YAW_RATE:
			_weight_yaw_rate = p_weight;
			break;
		default:
			_weight_extra = p_weight;
			break;
	}
	_dirty = true;
}

float MMCostFunction::get_group_weight(int p_group) const {
	switch (p_group) {
		case MM_GROUP_TRAJECTORY_POSITION:
			return _weight_trajectory_position;
		case MM_GROUP_TRAJECTORY_DIRECTION:
			return _weight_trajectory_direction;
		case MM_GROUP_POSE_POSITION:
			return _weight_pose_position;
		case MM_GROUP_POSE_VELOCITY:
			return _weight_pose_velocity;
		case MM_GROUP_ROOT_VELOCITY:
			return _weight_root_velocity;
		case MM_GROUP_YAW_RATE:
			return _weight_yaw_rate;
		default:
			return _weight_extra;
	}
}

// A group with 24 dimensions must not outweigh a group with 3 just because it
// is wider, so the authored weight is divided by the size of its group. What
// the designer types is therefore a true percentage of the total cost.
void MMCostFunction::rebuild(const Ref<MMFeatureSchema> &p_schema) {
	if (p_schema.is_valid()) {
		_schema = p_schema;
	}
	ERR_FAIL_COND_MSG(_schema.is_null(), "MMCostFunction requires a feature schema.");

	const int dimension = _schema->get_dimension();
	_dimension_weights.resize(dimension);
	float *weights = _dimension_weights.ptrw();

	int group_size[MM_GROUP_MAX] = { 0 };
	for (int i = 0; i < dimension; i++) {
		group_size[_schema->get_group_of_dimension(i)]++;
	}

	for (int i = 0; i < dimension; i++) {
		const int group = _schema->get_group_of_dimension(i);
		const int size = group_size[group] > 0 ? group_size[group] : 1;
		weights[i] = get_group_weight(group) / (float)size;
	}

	_dirty = false;
}

float MMCostFunction::compute_cost(const PackedFloat32Array &p_query,
		const PackedFloat32Array &p_frame) const {
	const int dimension = MIN(p_query.size(), p_frame.size());
	if (_dimension_weights.size() < dimension) {
		return MM_INFINITY;
	}
	return compute_raw(p_query.ptr(), p_frame.ptr(), _dimension_weights.ptr(), dimension, MM_INFINITY);
}

PackedFloat32Array MMCostFunction::compute_group_errors(const PackedFloat32Array &p_query,
		const PackedFloat32Array &p_frame) const {
	PackedFloat32Array errors;
	errors.resize(MM_GROUP_MAX);
	errors.fill(0.0f);

	if (_schema.is_null()) {
		return errors;
	}

	const int dimension = MIN(p_query.size(), p_frame.size());
	const float *weights = _dimension_weights.ptr();
	float *out = errors.ptrw();

	for (int i = 0; i < dimension; i++) {
		const float delta = p_query[i] - p_frame[i];
		const int group = _schema->get_group_of_dimension(i);
		out[group] += weights[i] * delta * delta;
	}
	return errors;
}

void MMCostFunction::_bind_methods() {
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_trajectory_position)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_trajectory_direction)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_pose_position)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_pose_velocity)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_root_velocity)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_extra)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, weight_yaw_rate)
	MM_BIND_PROPERTY(MMCostFunction, Variant::FLOAT, switch_penalty)

	ClassDB::bind_method(D_METHOD("set_schema", "schema"), &MMCostFunction::set_schema);
	ClassDB::bind_method(D_METHOD("get_schema"), &MMCostFunction::get_schema);
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "schema", PROPERTY_HINT_RESOURCE_TYPE, "MMFeatureSchema"),
			"set_schema", "get_schema");

	ClassDB::bind_method(D_METHOD("set_group_weight", "group", "weight"), &MMCostFunction::set_group_weight);
	ClassDB::bind_method(D_METHOD("get_group_weight", "group"), &MMCostFunction::get_group_weight);
	ClassDB::bind_method(D_METHOD("rebuild", "schema"), &MMCostFunction::rebuild);
	ClassDB::bind_method(D_METHOD("compute_cost", "query", "frame"), &MMCostFunction::compute_cost);
	ClassDB::bind_method(D_METHOD("compute_group_errors", "query", "frame"), &MMCostFunction::compute_group_errors);
	ClassDB::bind_method(D_METHOD("get_dimension_weight_table"), &MMCostFunction::get_dimension_weight_table);
}
