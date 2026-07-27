#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

MotionMatchingEditorPlugin::MotionMatchingEditorPlugin() {
	_root = memnew(VBoxContainer);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	toolbar->add_child(spacer);
	_fullscreen_button = memnew(Button);
	_fullscreen_button->set_text("Full Screen");
	_fullscreen_button->set_tooltip_text(
			"Open this dock in its own large window, so the Database, Features, "
			"Trajectory and Profiler tabs stop competing for space with the bottom "
			"panel's fixed height.");
	toolbar->add_child(_fullscreen_button);
	_root->add_child(toolbar);

	_panel = memnew(TabContainer);
	_panel->set_custom_minimum_size(Vector2(0, 320));
	_panel->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	_root->add_child(_panel);

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
	_bottom_panel_button = add_control_to_bottom_panel(_root, "Motion Matching");
	_fullscreen_button->connect("pressed", Callable(this, "_on_fullscreen_pressed"));
	if (_bottom_panel_button != nullptr) {
		_bottom_panel_button->connect("toggled", Callable(this, "_on_bottom_panel_toggled"));
	}
}

void MotionMatchingEditorPlugin::_exit_tree() {
	// If the dock is currently floated out, bring _panel back into _root
	// first -- otherwise memdelete(_root) below would never reach it, and
	// closing the window afterwards would touch a freed _panel.
	if (_fullscreen_window != nullptr) {
		_on_fullscreen_window_close_requested();
		memdelete(_fullscreen_window);
		_fullscreen_window = nullptr;
	}

	remove_control_from_bottom_panel(_root);
	if (_root != nullptr) {
		memdelete(_root);
		_root = nullptr;
		_panel = nullptr;
	}
}

void MotionMatchingEditorPlugin::_on_fullscreen_pressed() {
	if (_panel == nullptr) {
		return;
	}
	if (_fullscreen_window != nullptr && _panel->get_parent() == _fullscreen_window) {
		// Already floated out from an earlier press -- just bring the
		// existing window forward instead of trying to reparent again.
		_fullscreen_window->show();
		_fullscreen_window->grab_focus();
		return;
	}

	if (_fullscreen_window == nullptr) {
		_fullscreen_window = memnew(Window);
		_fullscreen_window->set_title("Motion Matching");
		_fullscreen_window->connect("close_requested", Callable(this, "_on_fullscreen_window_close_requested"));
		EditorInterface::get_singleton()->get_base_control()->add_child(_fullscreen_window);
	}

	_root->remove_child(_panel);
	_fullscreen_window->add_child(_panel);
	_panel->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);

	// An ordinary, OS decorated window sized to 95% of the screen -- not
	// exclusive fullscreen video mode, which would fight the editor for the
	// whole display. The window's own native close button is the "X" that
	// sends this back to the bottom panel; there is no button drawn for it
	// inside the dock itself.
	_fullscreen_window->popup_centered_ratio(0.95f);
	_fullscreen_button->set_visible(false);

	// _root is left holding nothing but its toolbar row once _panel moves
	// out -- collapsing the bottom dock here instead of leaving that mostly
	// empty strip on screen is what makes opening the "Motion Matching" tab
	// go straight to full screen rather than to a half-empty docked panel.
	hide_bottom_panel();
}

void MotionMatchingEditorPlugin::_on_bottom_panel_toggled(bool p_visible) {
	// Only react to the tab being opened -- hide_bottom_panel() above will
	// itself fire this same signal with p_visible false right afterwards,
	// which must be a no-op here or every open would immediately re-close.
	if (p_visible) {
		_on_fullscreen_pressed();
	}
}

void MotionMatchingEditorPlugin::_on_fullscreen_window_close_requested() {
	if (_fullscreen_window == nullptr || _panel == nullptr || _root == nullptr) {
		return;
	}
	_fullscreen_window->remove_child(_panel);
	_root->add_child(_panel);
	_root->move_child(_panel, 1); // Below the toolbar row, same as its original position.
	_fullscreen_window->hide();
	_fullscreen_button->set_visible(true);
}

void MotionMatchingEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_fullscreen_pressed"), &MotionMatchingEditorPlugin::_on_fullscreen_pressed);
	ClassDB::bind_method(D_METHOD("_on_fullscreen_window_close_requested"),
			&MotionMatchingEditorPlugin::_on_fullscreen_window_close_requested);
	ClassDB::bind_method(D_METHOD("_on_bottom_panel_toggled", "visible"),
			&MotionMatchingEditorPlugin::_on_bottom_panel_toggled);
}

#endif // TOOLS_ENABLED
