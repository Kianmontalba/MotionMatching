#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"

#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/window.hpp>
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
	_readout->add_text(vformat("Feet            L=%s  R=%s\n",
			(bool)info.get("left_foot_contact", false) ? "contact" : "lifted",
			(bool)info.get("right_foot_contact", false) ? "contact" : "lifted"));

	if ((bool)info.get("traversal_active", false)) {
		static const char *traversal_names[] = {
			"None", "Step", "Low Vault", "High Vault", "Mantle", "Climb", "Slide Under"
		};
		const int traversal_type = (int)info.get("traversal_type", 0);
		const char *traversal_name = traversal_type >= 0 && traversal_type < 7
				? traversal_names[traversal_type]
				: "?";
		_readout->add_text(vformat("Traversal       %s\n", traversal_name));
	}

	if ((bool)info.get("budget_exceeded", false)) {
		_readout->push_color(Color(1, 0.6f, 0.3f));
		_readout->add_text("\nSearch budget exceeded: the result is the best found so far.\n");
		_readout->pop();
	}

	// Per-group share of the winning frame's cost. Percentages, not raw
	// numbers, because the raw error scale depends on the weights the user
	// has set and isn't meaningful on its own -- "pose 40%" is.
	const Dictionary breakdown = controller->get_debug_cost_breakdown();
	if (!breakdown.is_empty()) {
		_readout->add_text("\nSearch Cost:\n");
		static const char *group_order[] = {
			"pose_position", "pose_velocity", "trajectory_position",
			"trajectory_direction", "root_velocity", "extra"
		};
		for (const char *group : group_order) {
			if (breakdown.has(group)) {
				_readout->add_text(vformat("  %-20s %5.1f%%\n", group, (double)breakdown[group]));
			}
		}
	}

	// Exact-cost runners-up, not the fast path's pruned candidates -- see
	// MMPoseSearch::search_top_candidates_debug_raw() for why the two differ.
	const Array candidates = controller->get_debug_candidates(3);
	if (!candidates.is_empty()) {
		_readout->add_text("\nTop candidates:\n");
		static const char *rank_labels[] = { "Top", "2nd", "3rd" };
		for (int i = 0; i < candidates.size(); i++) {
			const Dictionary candidate = candidates[i];
			const String label = i < 3 ? String(rank_labels[i]) : vformat("#%d", i + 1);
			_readout->add_text(vformat("  %-4s %-24s cost %.4f  (L=%s R=%s)\n", label,
					candidate.get("clip_name", ""), (double)candidate.get("cost", 0.0),
					(bool)candidate.get("left_foot_contact", false) ? "1" : "0",
					(bool)candidate.get("right_foot_contact", false) ? "1" : "0"));
		}
	}

	// Movement intent + accel/decel. Forward/right are relative to the
	// character's own facing, not world space, so this reads the same
	// regardless of which way the character happens to be pointed.
	if (info.has("speed_current")) {
		_readout->add_text("\nMovement:\n");
		_readout->add_text(vformat("  Speed           %.2f -> %.2f m/s  (max %.2f)   accel %.2f m/s^2\n",
				(double)info.get("speed_current", 0.0), (double)info.get("speed_desired", 0.0),
				(double)info.get("speed_max", 0.0), (double)info.get("acceleration", 0.0)));
		_readout->add_text(vformat("  Intent          forward %+.0f%%  right %+.0f%%  stop %.0f%%\n",
				(double)info.get("intent_forward_percent", 0.0), (double)info.get("intent_right_percent", 0.0),
				(double)info.get("intent_stop_percent", 0.0)));
		_readout->add_text(vformat("  Turn intent     %+.1f deg\n", (double)info.get("turn_intent_degrees", 0.0)));
		if ((bool)info.get("turn_in_place_needed", false)) {
			_readout->push_color(Color(0.4f, 0.8f, 1.0f));
			_readout->add_text("  Turn-in-place needed\n");
			_readout->pop();
		}
	}

	// One-tick-offset comparison -- see MotionMatchingController's field
	// comments on _last_root_motion_delta for why this isn't same-instant.
	_readout->add_text(vformat("\nRoot motion      %.4f m   actual movement %.4f m\n",
			(double)info.get("root_motion_delta_length", 0.0),
			(double)info.get("actual_movement_delta_length", 0.0)));

	// Next foot-contact change within the clip actually playing right now.
	const Dictionary footstep = controller->get_debug_footstep_timing();
	if (footstep.has("next_left_change_seconds") || footstep.has("next_right_change_seconds")) {
		_readout->add_text("\nNext footstep:\n");
		if (footstep.has("next_left_change_seconds")) {
			_readout->add_text(vformat("  Left            in %.2f s\n", (double)footstep["next_left_change_seconds"]));
		}
		if (footstep.has("next_right_change_seconds")) {
			_readout->add_text(vformat("  Right           in %.2f s\n", (double)footstep["next_right_change_seconds"]));
		}
	}

	// How many frames survive gameplay filtering (tags/category/speed)
	// before any cost is even computed -- separate from frames_compared
	// above, which is post-pruning inside the tree search.
	const Dictionary filter_stats = controller->get_debug_filter_stats();
	if (!filter_stats.is_empty()) {
		_readout->add_text(vformat("\nFilter          %s / %s frames survive tags+category+speed\n",
				filter_stats.get("after_filter", 0), filter_stats.get("total_frames", 0)));
	}
}

void MMDebugTools::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_refresh_pressed"), &MMDebugTools::_on_refresh_pressed);
}

#endif // TOOLS_ENABLED
