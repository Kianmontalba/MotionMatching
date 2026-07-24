#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

MMDebugTools::MMDebugTools() {
	HBoxContainer *row = memnew(HBoxContainer);
	Label *label = memnew(Label);
	label->set_text("Controller path");
	label->set_custom_minimum_size(Vector2(180, 0));
	row->add_child(label);

	_controller_path = memnew(LineEdit);
	_controller_path->set_text("/root/Main/Player/MotionMatching");
	_controller_path->set_h_size_flags(SIZE_EXPAND_FILL);
	row->add_child(_controller_path);

	_refresh_button = memnew(Button);
	_refresh_button->set_text("Refresh");
	row->add_child(_refresh_button);
	add_child(row);

	_readout = memnew(RichTextLabel);
	_readout->set_v_size_flags(SIZE_EXPAND_FILL);
	_readout->set_custom_minimum_size(Vector2(0, 200));
	add_child(_readout);

	set_process(true);
}

void MMDebugTools::_process(double p_delta) {
	// Polling at four hertz is enough to read, and it keeps the editor from
	// spending its frame budget on a debug panel.
	_timer += (float)p_delta;
	if (_timer < 0.25f) {
		return;
	}
	_timer = 0.0f;
	_on_refresh_pressed();
}

void MMDebugTools::_on_refresh_pressed() {
	_readout->clear();

	Node *node = get_tree() != nullptr ? get_tree()->get_root()->get_node_or_null(
												NodePath(_controller_path->get_text()))
									   : nullptr;
	MotionMatchingController *controller = Object::cast_to<MotionMatchingController>(node);
	if (controller == nullptr) {
		_readout->add_text("No running controller at that path.\n"
						   "Run the project, then point this at the controller node.");
		return;
	}

	const Dictionary info = controller->get_debug_info();
	_readout->add_text(vformat("Clip            %s\n", info.get("clip", "")));
	_readout->add_text(vformat("Time            %.3f s   frame %s\n", (double)info.get("time", 0.0),
			info.get("frame", -1)));
	_readout->add_text(vformat("Blending from   %s  (weight %.2f)\n", info.get("previous_clip", ""),
			(double)info.get("blend_weight", 1.0)));
	_readout->add_text(vformat("Match cost      %.4f   continuation %.4f\n",
			(double)info.get("match_cost", 0.0), (double)info.get("continuation_cost", 0.0)));
	_readout->add_text(vformat("Search          %.1f us   %s frames compared, %s candidates\n",
			(double)info.get("search_time_usec", 0.0), info.get("frames_compared", 0),
			info.get("candidates_visited", 0)));
	_readout->add_text(vformat("Tree nodes      %s\n", info.get("nodes_visited", 0)));
	_readout->add_text(vformat("Cache           %s hits / %s misses\n", info.get("cache_hits", 0),
			info.get("cache_misses", 0)));
	_readout->add_text(vformat("Database        %s frames\n", info.get("database_frames", 0)));
	_readout->add_text(vformat("Grounded        %s   time in clip %.2f s\n", info.get("grounded", true),
			(double)info.get("time_in_clip", 0.0)));

	if ((bool)info.get("budget_exceeded", false)) {
		_readout->push_color(Color(1, 0.6f, 0.3f));
		_readout->add_text("\nSearch budget exceeded: the result is the best found so far.\n");
		_readout->pop();
	}
}

void MMDebugTools::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_refresh_pressed"), &MMDebugTools::_on_refresh_pressed);
}

#endif // TOOLS_ENABLED
