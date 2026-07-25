#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

MotionMatchingEditorPlugin::MotionMatchingEditorPlugin() {
	_panel = memnew(TabContainer);
	_panel->set_custom_minimum_size(Vector2(0, 320));

	_database_editor = memnew(MMDatabaseEditor);
	_database_editor->set_name("Database");
	_panel->add_child(_database_editor);

	_feature_editor = memnew(MMFeatureEditor);
	_feature_editor->set_name("Features and Weights");
	_panel->add_child(_feature_editor);

	_trajectory_editor = memnew(MMTrajectoryEditor);
	_trajectory_editor->set_name("Trajectory");
	_panel->add_child(_trajectory_editor);

	_debug_tools = memnew(MMDebugTools);
	_debug_tools->set_name("Profiler");
	_panel->add_child(_debug_tools);

	// The database builder needs the schema the feature editor owns, so the
	// two tools share one resource instead of each holding a copy.
	_database_editor->set_schema(_feature_editor->get_schema());
}

String MotionMatchingEditorPlugin::_get_plugin_name() const {
	return "Motion Matching";
}

void MotionMatchingEditorPlugin::_enter_tree() {
	add_control_to_bottom_panel(_panel, "Motion Matching");
}

void MotionMatchingEditorPlugin::_exit_tree() {
	remove_control_from_bottom_panel(_panel);
	if (_panel != nullptr) {
		memdelete(_panel);
		_panel = nullptr;
	}
}

void MotionMatchingEditorPlugin::_bind_methods() {
}

#endif // TOOLS_ENABLED
