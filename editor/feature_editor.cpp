#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

static const char *MM_GROUP_NAMES[MM_GROUP_MAX] = {
	"Trajectory position", "Trajectory direction", "Pose position",
	"Pose velocity", "Root velocity", "Extra", "Yaw rate"
};

MMFeatureEditor::MMFeatureEditor() {
	_schema = MMFeatureSchema::make_default();
	_cost_function.instantiate();
	_cost_function->set_schema(_schema);
	_cost_function->rebuild(_schema);

	Label *title = memnew(Label);
	title->set_text("Schema");
	add_child(title);

	HBoxContainer *times_row = memnew(HBoxContainer);
	Label *times_label = memnew(Label);
	times_label->set_text("Trajectory times (s)");
	times_label->set_custom_minimum_size(Vector2(180, 0));
	times_row->add_child(times_label);
	_trajectory_times = memnew(LineEdit);
	_trajectory_times->set_text("0.2, 0.4, 0.6, 1.0");
	_trajectory_times->set_h_size_flags(SIZE_EXPAND_FILL);
	times_row->add_child(_trajectory_times);
	add_child(times_row);

	HBoxContainer *bones_row = memnew(HBoxContainer);
	Label *bones_label = memnew(Label);
	bones_label->set_text("Pose bones");
	bones_label->set_custom_minimum_size(Vector2(180, 0));
	bones_row->add_child(bones_label);
	_pose_bones = memnew(LineEdit);
	_pose_bones->set_text("Hips, LeftFoot, RightFoot");
	_pose_bones->set_h_size_flags(SIZE_EXPAND_FILL);
	bones_row->add_child(_pose_bones);
	add_child(bones_row);

	HBoxContainer *root_row = memnew(HBoxContainer);
	Label *root_label = memnew(Label);
	root_label->set_text("Root bone");
	root_label->set_custom_minimum_size(Vector2(180, 0));
	root_row->add_child(root_label);
	_root_bone = memnew(LineEdit);
	_root_bone->set_text("Root");
	_root_bone->set_h_size_flags(SIZE_EXPAND_FILL);
	root_row->add_child(_root_bone);
	add_child(root_row);

	_include_bone_velocity = memnew(CheckBox);
	_include_bone_velocity->set_text("Include bone velocity");
	_include_bone_velocity->set_pressed(true);
	add_child(_include_bone_velocity);

	_include_root_velocity = memnew(CheckBox);
	_include_root_velocity->set_text("Include root velocity");
	_include_root_velocity->set_pressed(true);
	add_child(_include_root_velocity);

	_dimension_label = memnew(Label);
	add_child(_dimension_label);

	Label *weights_title = memnew(Label);
	weights_title->set_text("Cost weights");
	add_child(weights_title);

	// One slider per feature group. Because the cost function divides each
	// weight by the size of its group, these read as a true share of the total
	// cost rather than an arbitrary multiplier.
	for (int i = 0; i < MM_GROUP_MAX; i++) {
		HBoxContainer *row = memnew(HBoxContainer);
		Label *label = memnew(Label);
		label->set_text(MM_GROUP_NAMES[i]);
		label->set_custom_minimum_size(Vector2(180, 0));
		row->add_child(label);

		_weights[i] = memnew(HSlider);
		_weights[i]->set_min(0.0);
		_weights[i]->set_max(3.0);
		_weights[i]->set_step(0.05);
		_weights[i]->set_value(_cost_function->get_group_weight(i));
		_weights[i]->set_h_size_flags(SIZE_EXPAND_FILL);
		row->add_child(_weights[i]);

		_weight_labels[i] = memnew(Label);
		_weight_labels[i]->set_custom_minimum_size(Vector2(60, 0));
		row->add_child(_weight_labels[i]);

		add_child(row);
	}

	_apply_button = memnew(Button);
	_apply_button->set_text("Apply");
	add_child(_apply_button);

	_refresh();
}

void MMFeatureEditor::set_schema(const Ref<MMFeatureSchema> &p_schema) {
	_schema = p_schema;
	_cost_function->set_schema(_schema);
	_cost_function->rebuild(_schema);
	_refresh();
}

void MMFeatureEditor::_refresh() {
	if (_schema.is_null()) {
		return;
	}
	_dimension_label->set_text(vformat("Vector dimension: %d  (low LOD %d, medium %d, high %d)",
			_schema->get_dimension(), _schema->get_lod_dimension(MM_QUALITY_LOW),
			_schema->get_lod_dimension(MM_QUALITY_MEDIUM),
			_schema->get_lod_dimension(MM_QUALITY_HIGH)));

	float total = 0.0f;
	for (int i = 0; i < MM_GROUP_MAX; i++) {
		total += _cost_function->get_group_weight(i);
	}
	for (int i = 0; i < MM_GROUP_MAX; i++) {
		const float weight = _cost_function->get_group_weight(i);
		const float share = total > 0.0f ? weight / total * 100.0f : 0.0f;
		_weight_labels[i]->set_text(vformat("%.0f%%", share));
	}
}

void MMFeatureEditor::_on_apply_pressed() {
	PackedFloat32Array times;
	const PackedStringArray time_parts = _trajectory_times->get_text().split(",", false);
	for (int i = 0; i < time_parts.size(); i++) {
		times.push_back(time_parts[i].strip_edges().to_float());
	}

	PackedStringArray bones;
	const PackedStringArray bone_parts = _pose_bones->get_text().split(",", false);
	for (int i = 0; i < bone_parts.size(); i++) {
		bones.push_back(bone_parts[i].strip_edges());
	}

	_schema->set_trajectory_times(times);
	_schema->set_pose_bones(bones);
	_schema->set_root_bone(_root_bone->get_text().strip_edges());
	_schema->set_include_bone_velocity(_include_bone_velocity->is_pressed());
	_schema->set_include_root_velocity(_include_root_velocity->is_pressed());

	for (int i = 0; i < MM_GROUP_MAX; i++) {
		_cost_function->set_group_weight(i, (float)_weights[i]->get_value());
	}
	_cost_function->rebuild(_schema);
	_refresh();
}

void MMFeatureEditor::_on_weight_changed(float p_value) {
	for (int i = 0; i < MM_GROUP_MAX; i++) {
		_cost_function->set_group_weight(i, (float)_weights[i]->get_value());
	}
	_refresh();
}

void MMFeatureEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_apply_pressed"), &MMFeatureEditor::_on_apply_pressed);
	ClassDB::bind_method(D_METHOD("_on_weight_changed", "value"), &MMFeatureEditor::_on_weight_changed);
}

#endif // TOOLS_ENABLED
