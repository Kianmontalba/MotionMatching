#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"
#include "animation_library.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/scene_tree.hpp>

using namespace godot;

// ---------------------------------------------------------------------------
// MMNodePathField
// ---------------------------------------------------------------------------

bool MMNodePathField::_can_drop_data(const Vector2 &p_point, const Variant &p_data) const {
	if (p_data.get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary data = p_data;
	// This is the same drag payload shape the Scene dock hands to every
	// built-in NodePath field in the Inspector -- matching it means this
	// field accepts a drop exactly where a person would already expect one
	// to work.
	return String(data.get("type", "")) == "nodes";
}

void MMNodePathField::_drop_data(const Vector2 &p_point, const Variant &p_data) {
	const Dictionary data = p_data;
	const Array dropped = data.get("nodes", Array());
	if (dropped.is_empty()) {
		return;
	}

	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (scene_root == nullptr || scene_root->get_tree() == nullptr) {
		return;
	}

	// Resolved through the live SceneTree rather than string-parsed: the
	// dropped path from the Scene dock has always been absolute in every
	// Godot 4 release this addon targets, but going through the actual
	// Node means a future change to that format cannot silently write a
	// wrong path in here -- it would just fail to resolve and the drop
	// would be quietly ignored instead.
	Node *dropped_node = scene_root->get_tree()->get_root()->get_node_or_null(NodePath(dropped[0]));
	if (dropped_node == nullptr) {
		// Fall back to treating the payload as already relative to the
		// edited scene root, in case that assumption above ever changes.
		dropped_node = scene_root->get_node_or_null(NodePath(dropped[0]));
	}
	if (dropped_node == nullptr) {
		return;
	}

	// Always re-expressed relative to the edited scene root, not copied
	// verbatim -- this is what _resolve_skeleton() expects, and what keeps
	// the path correct if the scene is ever renamed or moved.
	set_text(String(scene_root->get_path_to(dropped_node)));
	grab_focus();
	emit_signal("text_changed", get_text());
	emit_signal("text_submitted", get_text());
}

void MMNodePathField::_bind_methods() {
}

// Small helper for the repetitive "label plus field on one row" layout.
static HBoxContainer *mm_add_row(VBoxContainer *p_parent, const String &p_label, Control *p_control) {
	HBoxContainer *row = memnew(HBoxContainer);
	Label *label = memnew(Label);
	label->set_text(p_label);
	label->set_custom_minimum_size(Vector2(180, 0));
	row->add_child(label);
	p_control->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	row->add_child(p_control);
	p_parent->add_child(row);
	return row;
}

MMDatabaseEditor::MMDatabaseEditor() {
	_resource_picker = memnew(EditorResourcePicker);
	_resource_picker->set_base_type("MotionMatchingResource");
	mm_add_row(this, "Motion Matching Resource", _resource_picker);

	_skeleton_path = memnew(MMNodePathField);
	_skeleton_path->set_text("%GeneralSkeleton");
	_skeleton_path->set_placeholder("Type a path, drag a node in from the Scene dock, or use Pick...");
	_skeleton_path->set_h_size_flags(Control::SIZE_EXPAND_FILL);

	_skeleton_picker_button = memnew(Button);
	_skeleton_picker_button->set_text("Pick...");
	_skeleton_picker_button->set_tooltip_text("Browse every Skeleton3D node in the current scene");

	HBoxContainer *skeleton_row = memnew(HBoxContainer);
	skeleton_row->add_child(_skeleton_path);
	skeleton_row->add_child(_skeleton_picker_button);
	mm_add_row(this, "Skeleton node path", skeleton_row);

	_skeleton_popup = memnew(PopupMenu);
	add_child(_skeleton_popup);

	// Backed by the resource above, not a typed-in path: picking (or
	// dropping) a library here writes it straight onto the Motion Matching
	// Resource and saves that resource to disk immediately, so it survives
	// closing the dock, switching scenes, or an addon reload -- none of
	// which this control's own state would otherwise survive.
	_library_picker = memnew(EditorResourcePicker);
	_library_picker->set_base_type("AnimationLibrary");
	mm_add_row(this, "Animation library", _library_picker);

	_box_option = memnew(OptionButton);
	_box_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_add_box_button = memnew(Button);
	_add_box_button->set_text("+ Add Box");
	_add_box_button->set_tooltip_text("New animation set, e.g. \"Normal Rifle\", \"Pistol\", \"Zombie\"");
	HBoxContainer *box_row = memnew(HBoxContainer);
	box_row->add_child(_box_option);
	box_row->add_child(_add_box_button);
	mm_add_row(this, "Animation set (box)", box_row);

	_box_name_edit = memnew(LineEdit);
	_box_name_edit->set_placeholder("Name this set, e.g. Pistol");
	mm_add_row(this, "Box name", _box_name_edit);

	_database_option = memnew(OptionButton);
	_database_option->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	_add_database_button = memnew(Button);
	_add_database_button->set_text("+ Add Database");
	_add_database_button->set_tooltip_text("New database inside this box, e.g. \"Walk\", \"Sprint\", \"Jump\"");
	HBoxContainer *database_row = memnew(HBoxContainer);
	database_row->add_child(_database_option);
	database_row->add_child(_add_database_button);
	mm_add_row(this, "Database", database_row);

	_database_name_edit = memnew(LineEdit);
	_database_name_edit->set_placeholder("Name this database, e.g. Walk");
	mm_add_row(this, "Database name", _database_name_edit);

	_loop_toggle = memnew(CheckButton);
	_loop_toggle->set_text("Loop every clip in this database");
	mm_add_row(this, "Toggle loop (all clips)", _loop_toggle);

	_sample_rate = memnew(SpinBox);
	_sample_rate->set_min(10);
	_sample_rate->set_max(120);
	_sample_rate->set_value(30);
	mm_add_row(this, "Sample rate (Hz)", _sample_rate);

	_root_yaw_offset = memnew(SpinBox);
	_root_yaw_offset->set_min(-180);
	_root_yaw_offset->set_max(180);
	_root_yaw_offset->set_step(1);
	_root_yaw_offset->set_value(0);
	mm_add_row(this, "Root yaw offset (degrees)", _root_yaw_offset);

	_left_foot_override = memnew(LineEdit);
	_left_foot_override->set_placeholder("Leave empty to auto-detect");
	mm_add_row(this, "Left foot bone (override)", _left_foot_override);

	_right_foot_override = memnew(LineEdit);
	_right_foot_override->set_placeholder("Leave empty to auto-detect");
	mm_add_row(this, "Right foot bone (override)", _right_foot_override);

	HBoxContainer *buttons = memnew(HBoxContainer);
	_scan_button = memnew(Button);
	_scan_button->set_text("Scan library");
	buttons->add_child(_scan_button);

	_validate_button = memnew(Button);
	_validate_button->set_text("Validate");
	buttons->add_child(_validate_button);

	_build_button = memnew(Button);
	_build_button->set_text("Build database");
	buttons->add_child(_build_button);

	_save_button = memnew(Button);
	_save_button->set_text("Save");
	buttons->add_child(_save_button);
	add_child(buttons);

	_progress = memnew(ProgressBar);
	_progress->set_max(1.0);
	add_child(_progress);

	_clip_table = memnew(Tree);
	_clip_table->set_columns(4);
	_clip_table->set_column_titles_visible(true);
	_clip_table->set_column_title(0, "Clip");
	_clip_table->set_column_title(1, "Category");
	_clip_table->set_column_title(2, "Tags");
	_clip_table->set_column_title(3, "Length");
	_clip_table->set_v_size_flags(SIZE_EXPAND_FILL);
	_clip_table->set_custom_minimum_size(Vector2(0, 160));
	add_child(_clip_table);

	_log = memnew(RichTextLabel);
	_log->set_custom_minimum_size(Vector2(0, 90));
	add_child(_log);
}

void MMDatabaseEditor::_ready() {
	_resource_picker->connect("resource_changed", Callable(this, "_on_resource_picked"));
	_library_picker->connect("resource_changed", Callable(this, "_on_library_picked"));
	_skeleton_path->connect("text_changed", Callable(this, "_on_skeleton_path_changed"));
	_skeleton_picker_button->connect("pressed", Callable(this, "_on_skeleton_picker_pressed"));
	_skeleton_popup->connect("index_pressed", Callable(this, "_on_skeleton_popup_selected"));
	_left_foot_override->connect("text_changed", Callable(this, "_on_left_foot_override_changed"));
	_right_foot_override->connect("text_changed", Callable(this, "_on_right_foot_override_changed"));
	_box_option->connect("item_selected", Callable(this, "_on_box_selected"));
	_add_box_button->connect("pressed", Callable(this, "_on_add_box_pressed"));
	_box_name_edit->connect("text_changed", Callable(this, "_on_box_name_changed"));
	_database_option->connect("item_selected", Callable(this, "_on_database_selected"));
	_add_database_button->connect("pressed", Callable(this, "_on_add_database_pressed"));
	_database_name_edit->connect("text_changed", Callable(this, "_on_database_name_changed"));
	_loop_toggle->connect("toggled", Callable(this, "_on_loop_toggle_toggled"));
	_scan_button->connect("pressed", Callable(this, "_on_scan_pressed"));
	_build_button->connect("pressed", Callable(this, "_on_build_pressed"));
	_save_button->connect("pressed", Callable(this, "_on_save_pressed"));
	_validate_button->connect("pressed", Callable(this, "_on_validate_pressed"));
	_log_line("Ready. Assign a Motion Matching Resource, point at a Skeleton3D, then scan a library.");
}

void MMDatabaseEditor::_log_line(const String &p_text, const Color &p_color) {
	if (_log == nullptr) {
		return;
	}
	_log->push_color(p_color);
	_log->add_text(p_text + String("\n"));
	_log->pop();
}

Skeleton3D *MMDatabaseEditor::_resolve_skeleton() {
	Node *root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (root == nullptr) {
		_log_line("No scene is open.", Color(1, 0.5f, 0.4f));
		return nullptr;
	}
	Node *node = root->get_node_or_null(NodePath(_skeleton_path->get_text()));
	Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(node);
	if (skeleton == nullptr) {
		_log_line("Could not resolve a Skeleton3D at that path.", Color(1, 0.5f, 0.4f));
	}
	return skeleton;
}

void MMDatabaseEditor::_on_resource_picked(const Ref<Resource> &p_resource) {
	_mm_resource = p_resource;
	if (_mm_resource.is_null()) {
		_log_line("Motion Matching Resource cleared.", Color(1, 0.85f, 0.4f));
		return;
	}

	// Load whatever this resource already has assigned, rather than leaving
	// the dock showing something unrelated to the resource just picked.
	_library_picker->set_edited_resource(_mm_resource->get_animation_library());
	_library = _mm_resource->get_animation_library();
	_extra_database = _mm_resource->get_database();
	_schema = _mm_resource->get_schema();
	_refresh_box_options();
	if (!_mm_resource->get_skeleton_path().is_empty()) {
		_skeleton_path->set_text(String(_mm_resource->get_skeleton_path()));
	}
	_left_foot_override->set_text(_mm_resource->get_left_foot_override());
	_right_foot_override->set_text(_mm_resource->get_right_foot_override());

	if (_library.is_valid()) {
		_clip_settings = MMAnimationLibraryTools::auto_tag_library(_library);
		_populate_table();
	} else {
		_clip_table->clear();
	}

	_log_line("Loaded " + _mm_resource->get_path(), Color(0.5f, 1.0f, 0.6f));
}

void MMDatabaseEditor::_on_library_picked(const Ref<Resource> &p_resource) {
	_library = p_resource;

	if (_mm_resource.is_valid()) {
		// Persisted immediately, not just held in the dock's own memory --
		// this is what makes the pick survive closing the dock or an addon
		// reload, unlike a typed-in path that lived only in a LineEdit.
		_mm_resource->set_animation_library(_library);
		if (!_mm_resource->get_path().is_empty()) {
			const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
			if (error != OK) {
				_log_line(vformat("Could not save the library pick back to %s (error %d).",
								_mm_resource->get_path(), (int)error),
						Color(1, 0.5f, 0.4f));
			}
		} else {
			_log_line(
					"Motion Matching Resource has not been saved to disk yet, so this pick will "
					"only last for the current editor session. Save it once from the Inspector "
					"and the pick will start persisting automatically.",
					Color(1, 0.85f, 0.4f));
		}
	}

	if (_library.is_null()) {
		_clip_table->clear();
		return;
	}

	// Auto tagging is a starting point, not the answer. The table stays
	// editable so a wrong guess costs one click, not a rebuild.
	_clip_settings = MMAnimationLibraryTools::auto_tag_library(_library);
	_populate_table();
	_log_line(vformat("Scanned %d clips and suggested tags for each.",
			_library->get_animation_list().size()));
}

void MMDatabaseEditor::_on_skeleton_path_changed(const String &p_text) {
	if (_mm_resource.is_null()) {
		return;
	}
	// Same persist-immediately pattern as _on_library_picked(): written onto
	// the resource (and saved to disk, if it has one) as soon as it changes,
	// so a dropped or typed path survives closing the dock, switching
	// scenes, or an addon reload, instead of resetting to the placeholder.
	_mm_resource->set_skeleton_path(NodePath(p_text));
	if (_mm_resource->get_path().is_empty()) {
		return;
	}
	const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	if (error != OK) {
		_log_line(vformat("Could not save the skeleton path back to %s (error %d).",
							_mm_resource->get_path(), (int)error),
				Color(1, 0.5f, 0.4f));
	}
}

void MMDatabaseEditor::_on_left_foot_override_changed(const String &p_text) {
	if (_mm_resource.is_null()) {
		return;
	}
	// Same persist-immediately pattern as _on_skeleton_path_changed().
	_mm_resource->set_left_foot_override(p_text);
	if (_mm_resource->get_path().is_empty()) {
		return;
	}
	const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	if (error != OK) {
		_log_line(vformat("Could not save the left foot override back to %s (error %d).",
							_mm_resource->get_path(), (int)error),
				Color(1, 0.5f, 0.4f));
	}
}

void MMDatabaseEditor::_on_right_foot_override_changed(const String &p_text) {
	if (_mm_resource.is_null()) {
		return;
	}
	_mm_resource->set_right_foot_override(p_text);
	if (_mm_resource->get_path().is_empty()) {
		return;
	}
	const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	if (error != OK) {
		_log_line(vformat("Could not save the right foot override back to %s (error %d).",
							_mm_resource->get_path(), (int)error),
				Color(1, 0.5f, 0.4f));
	}
}

void MMDatabaseEditor::_persist_extra_database() {
	if (_mm_resource.is_null()) {
		return;
	}
	// Same persist-immediately pattern as _on_skeleton_path_changed(): the
	// whole box/database structure is written onto the resource (and saved
	// to disk, if it has one) as soon as it changes, so adding, renaming or
	// switching a box/database survives closing the dock -- nothing here
	// resets like the old manual bone UI used to.
	_mm_resource->set_database(_extra_database);
	if (_mm_resource->get_path().is_empty()) {
		return;
	}
	const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	if (error != OK) {
		_log_line(vformat("Could not save the animation set structure back to %s (error %d).",
							_mm_resource->get_path(), (int)error),
				Color(1, 0.5f, 0.4f));
	}
}

void MMDatabaseEditor::_refresh_box_options() {
	_box_option->clear();
	if (_extra_database.is_null()) {
		_box_name_edit->set_text("");
		_refresh_database_options();
		return;
	}
	const int count = _extra_database->get_box_count();
	for (int i = 0; i < count; i++) {
		Ref<MMBoxAnimation> box = _extra_database->get_box(i);
		const String label = (box.is_valid() && !box->get_name().is_empty()) ? box->get_name() : vformat("Box %d", i + 1);
		_box_option->add_item(label);
	}
	if (count == 0) {
		_box_name_edit->set_text("");
		_refresh_database_options();
		return;
	}
	int active = _extra_database->get_active_box_index();
	if (active < 0 || active >= count) {
		active = 0;
	}
	_box_option->select(active);
	Ref<MMBoxAnimation> box = _extra_database->get_box(active);
	_box_name_edit->set_text(box.is_valid() ? box->get_name() : "");
	_refresh_database_options();
}

void MMDatabaseEditor::_refresh_database_options() {
	_database_option->clear();
	Ref<MMBoxAnimation> box;
	if (_extra_database.is_valid() && _extra_database->get_box_count() > 0) {
		int active_box = _extra_database->get_active_box_index();
		if (active_box < 0 || active_box >= _extra_database->get_box_count()) {
			active_box = 0;
		}
		box = _extra_database->get_box(active_box);
	}
	if (box.is_null()) {
		_database = Ref<MotionMatchingDatabase>();
		_database_name_edit->set_text("");
		return;
	}
	const int count = box->get_database_count();
	for (int i = 0; i < count; i++) {
		Ref<MotionMatchingDatabase> database = box->get_database(i);
		const String label = (database.is_valid() && !database->get_name().is_empty()) ? database->get_name() : vformat("Database %d", i + 1);
		_database_option->add_item(label);
	}
	if (count == 0) {
		_database = Ref<MotionMatchingDatabase>();
		_database_name_edit->set_text("");
		return;
	}
	int active = _extra_database->get_active_database_index();
	if (active < 0 || active >= count) {
		active = 0;
	}
	_database_option->select(active);
	_database = box->get_database(active);
	_database_name_edit->set_text(_database.is_valid() ? _database->get_name() : "");
}

void MMDatabaseEditor::_on_box_selected(int p_index) {
	if (_extra_database.is_null()) {
		return;
	}
	_extra_database->set_active_box_index(p_index);
	_extra_database->set_active_database_index(0);
	_persist_extra_database();
	Ref<MMBoxAnimation> box = _extra_database->get_box(p_index);
	_box_name_edit->set_text(box.is_valid() ? box->get_name() : "");
	_refresh_database_options();
}

void MMDatabaseEditor::_on_add_box_pressed() {
	if (_mm_resource.is_null()) {
		_log_line("Assign a Motion Matching Resource above first.", Color(1, 0.5f, 0.4f));
		return;
	}
	if (_extra_database.is_null()) {
		_extra_database.instantiate();
	}
	// Unlimited -- this just keeps appending.
	_extra_database->add_box("New Box");
	_extra_database->set_active_box_index(_extra_database->get_box_count() - 1);
	_extra_database->set_active_database_index(0);
	_persist_extra_database();
	_refresh_box_options();
}

void MMDatabaseEditor::_on_box_name_changed(const String &p_text) {
	if (_extra_database.is_null()) {
		return;
	}
	Ref<MMBoxAnimation> box = _extra_database->get_box(_extra_database->get_active_box_index());
	if (box.is_null()) {
		return;
	}
	box->set_name(p_text);
	_persist_extra_database();
	// Keeps the dropdown's own label in sync without a full rebuild.
	_box_option->set_item_text(_extra_database->get_active_box_index(), p_text.is_empty() ? vformat("Box %d", _extra_database->get_active_box_index() + 1) : p_text);
}

void MMDatabaseEditor::_on_database_selected(int p_index) {
	if (_extra_database.is_null()) {
		return;
	}
	_extra_database->set_active_database_index(p_index);
	_persist_extra_database();
	Ref<MMBoxAnimation> box = _extra_database->get_box(_extra_database->get_active_box_index());
	_database = box.is_valid() ? box->get_database(p_index) : Ref<MotionMatchingDatabase>();
	_database_name_edit->set_text(_database.is_valid() ? _database->get_name() : "");
}

void MMDatabaseEditor::_on_add_database_pressed() {
	if (_extra_database.is_null() || _extra_database->get_box_count() == 0) {
		_log_line("Add an animation set (box) first -- use \"+ Add Box\" above.", Color(1, 0.85f, 0.4f));
		return;
	}
	Ref<MMBoxAnimation> box = _extra_database->get_box(_extra_database->get_active_box_index());
	if (box.is_null()) {
		return;
	}
	// Unlimited -- this just keeps appending, same as add_box().
	box->add_database("New Database");
	_extra_database->set_active_database_index(box->get_database_count() - 1);
	_persist_extra_database();
	_refresh_database_options();
}

void MMDatabaseEditor::_on_database_name_changed(const String &p_text) {
	if (_database.is_null()) {
		return;
	}
	_database->set_name(p_text);
	_persist_extra_database();
	if (_extra_database.is_valid()) {
		_database_option->set_item_text(_extra_database->get_active_database_index(),
				p_text.is_empty() ? vformat("Database %d", _extra_database->get_active_database_index() + 1) : p_text);
	}
}

void MMDatabaseEditor::_on_loop_toggle_toggled(bool p_pressed) {
	if (_database.is_null()) {
		_log_line("Pick or build a database first.", Color(1, 0.85f, 0.4f));
		_loop_toggle->set_pressed(false);
		return;
	}
	// Handled entirely inside MotionMatchingDatabase -- it walks its own
	// MMAnimationEntry list, this dock never touches entries directly.
	_database->set_all_loop(p_pressed);
	_log_line(vformat("Set loop = %s for all %d clip(s) in \"%s\".", p_pressed ? "on" : "off",
					  _database->get_animation_count(), _database->get_name()),
			Color(0.5f, 1.0f, 0.6f));
}

void MMDatabaseEditor::_on_scan_pressed() {
	// Re-reads whatever is currently assigned rather than a remembered path,
	// so pressing Scan after e.g. calling refresh_all() on an
	// MMAnimationLibrary picks up files that were added or removed since the
	// library was first picked.
	_on_library_picked(_library_picker->get_edited_resource());
}

void MMDatabaseEditor::_populate_table() {
	_clip_table->clear();
	if (_library.is_null()) {
		return;
	}

	TreeItem *root = _clip_table->create_item();
	_clip_table->set_hide_root(true);

	const PackedStringArray names = _library->get_animation_list();
	static const char *category_names[] = { "Locomotion", "Airborne", "Traversal", "Combat",
		"Interaction", "Custom" };

	for (int i = 0; i < names.size(); i++) {
		Ref<Animation> animation = _library->get_animation(names[i]);
		const Dictionary settings = _clip_settings.get(names[i], Dictionary());
		const int category = settings.get("category", 0);
		const int tags = settings.get("tags", 0);

		TreeItem *item = _clip_table->create_item(root);
		item->set_text(0, names[i]);
		item->set_text(1, category >= 0 && category < MM_CATEGORY_MAX ? category_names[category] : "?");
		item->set_text(2, String::num_int64(tags));
		item->set_text(3, animation.is_valid() ? vformat("%.2fs", animation->get_length()) : "-");
		item->set_editable(2, true);
	}
}

void MMDatabaseEditor::_collect_skeletons(Node *p_node, Node *p_scene_root) {
	if (Object::cast_to<Skeleton3D>(p_node) != nullptr) {
		// Stored relative to the scene root, same convention _resolve_skeleton()
		// and the drag-and-drop path already use -- so picking one here writes
		// exactly the same kind of path a drop or a hand-typed one would.
		_skeleton_candidates.push_back(String(p_scene_root->get_path_to(p_node)));
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_collect_skeletons(p_node->get_child(i), p_scene_root);
	}
}

void MMDatabaseEditor::_on_skeleton_picker_pressed() {
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	if (scene_root == nullptr) {
		_log_line("No scene is open.", Color(1, 0.5f, 0.4f));
		return;
	}

	_skeleton_candidates.clear();
	_collect_skeletons(scene_root, scene_root);

	if (_skeleton_candidates.is_empty()) {
		_log_line("No Skeleton3D node found in the current scene.", Color(1, 0.85f, 0.4f));
		return;
	}

	_skeleton_popup->clear();
	for (int i = 0; i < _skeleton_candidates.size(); i++) {
		_skeleton_popup->add_item(_skeleton_candidates[i]);
	}

	// Dropped right under the button, the same place any other editor popup
	// (like an OptionButton) would open. Control has get_screen_position()
	// (screen-space) but no get_screen_rect(), so the rect is built by hand
	// from that plus the button's own size.
	const Vector2 button_pos = _skeleton_picker_button->get_screen_position();
	const Vector2 button_size = _skeleton_picker_button->get_size();
	_skeleton_popup->set_position(Vector2i(button_pos.x, button_pos.y + button_size.y));
	_skeleton_popup->popup();
}

void MMDatabaseEditor::_on_skeleton_popup_selected(int p_index) {
	if (p_index < 0 || p_index >= _skeleton_candidates.size()) {
		return;
	}
	_skeleton_path->set_text(_skeleton_candidates[p_index]);
	// MMNodePathField's own text_changed signal only fires on user typing,
	// not on a programmatic set_text() -- so this mirrors what dropping a
	// node already does, calling the same handler directly to persist the
	// pick onto the resource immediately.
	_on_skeleton_path_changed(_skeleton_candidates[p_index]);
}

void MMDatabaseEditor::_on_validate_pressed() {
	Skeleton3D *skeleton = _resolve_skeleton();
	if (skeleton == nullptr || _library.is_null()) {
		return;
	}
	if (_schema.is_null()) {
		_schema = MMFeatureSchema::make_default();
	}

	const Array issues = MMAnimationLibraryTools::validate_library(_library, skeleton, _schema);
	if (issues.is_empty()) {
		_log_line("Validation passed with no issues.", Color(0.5f, 1.0f, 0.6f));
		return;
	}
	for (int i = 0; i < MIN(issues.size(), 40); i++) {
		const Dictionary issue = issues[i];
		const bool error = String(issue.get("severity", "warning")) == "error";
		_log_line(vformat("[%s] %s %s", issue.get("severity", ""), issue.get("clip", ""),
						issue.get("message", "")),
				error ? Color(1, 0.5f, 0.4f) : Color(1, 0.85f, 0.4f));
	}
	if (issues.size() > 40) {
		_log_line(vformat("...and %d more.", issues.size() - 40));
	}
}

void MMDatabaseEditor::_on_build_pressed() {
	Skeleton3D *skeleton = _resolve_skeleton();
	if (skeleton == nullptr || _library.is_null()) {
		return;
	}
	if (_extra_database.is_null() || _extra_database->get_box_count() == 0) {
		_log_line("Add an animation set (box) first -- use \"+ Add Box\" above.", Color(1, 0.85f, 0.4f));
		return;
	}
	Ref<MMBoxAnimation> box = _extra_database->get_box(_extra_database->get_active_box_index());
	if (box.is_null() || box->get_database_count() == 0) {
		_log_line("Add a database to this box first -- use \"+ Add Database\" above.", Color(1, 0.85f, 0.4f));
		return;
	}
	if (_schema.is_null()) {
		_schema = MMFeatureSchema::make_default();
	}

	Ref<MMFeatureExtractor> extractor;
	extractor.instantiate();
	extractor->set_schema(_schema);
	extractor->set_sample_rate((float)_sample_rate->get_value());
	extractor->set_root_yaw_offset_degrees((float)_root_yaw_offset->get_value());

	const String left_foot = _left_foot_override->get_text().strip_edges();
	const String right_foot = _right_foot_override->get_text().strip_edges();
	if (!left_foot.is_empty() || !right_foot.is_empty()) {
		// Only these two roles are ever set by hand here -- everything else
		// stays on auto-detect (the extractor's auto_detect_profile default
		// is left untouched). MMSkeletonProfile locks a role the moment it is
		// set explicitly and keeps it through every future auto_detect() call,
		// so this survives rebuilds instead of being overwritten.
		Ref<MMSkeletonProfile> profile;
		profile.instantiate();
		if (!left_foot.is_empty()) {
			profile->set_bone_name(MM_BONE_LEFT_FOOT, left_foot);
		}
		if (!right_foot.is_empty()) {
			profile->set_bone_name(MM_BONE_RIGHT_FOOT, right_foot);
		}
		extractor->set_profile(profile);
	}

	_database = extractor->build_database(skeleton, _library, _clip_settings);
	_progress->set_value(1.0);

	if (_database.is_null()) {
		_log_line(
				"Build failed. Check the Output/Debugger panel for a \"Skeleton profile "
				"detection failed\" message -- this usually means the picked skeleton uses "
				"bone names auto-detect could not recognize.",
				Color(1, 0.5f, 0.4f));
		return;
	}

	// build_database() always returns a brand new object -- it never edits
	// one in place -- so the name typed into "Database name" above has to be
	// carried over by hand, and the box's slot has to be swapped rather than
	// just reassigning the dock's own _database.
	const int active_index = _extra_database->get_active_database_index();
	Ref<MotionMatchingDatabase> previous = box->get_database(active_index);
	_database->set_name(previous.is_valid() ? previous->get_name() : _database_name_edit->get_text());
	box->set_database_at(active_index, _database);
	_persist_extra_database();

	const Dictionary stats = _database->get_statistics();
	_log_line(vformat("Built %s frames from %s clips, %s dimensions, %.1f MB of features.",
					  stats["frame_count"], stats["animation_count"], stats["dimension"],
					  (double)(int64_t)stats["feature_bytes"] / 1048576.0),
			Color(0.5f, 1.0f, 0.6f));
}

void MMDatabaseEditor::_save_all_databases() {
	if (_extra_database.is_null() || _mm_resource.is_null() || _mm_resource->get_path().is_empty()) {
		return;
	}
	const String base_dir = _mm_resource->get_path().get_base_dir();
	for (int b = 0; b < _extra_database->get_box_count(); b++) {
		Ref<MMBoxAnimation> box = _extra_database->get_box(b);
		if (box.is_null()) {
			continue;
		}
		const String box_name = box->get_name().is_empty() ? vformat("box%d", b + 1) : box->get_name();
		for (int d = 0; d < box->get_database_count(); d++) {
			Ref<MotionMatchingDatabase> database = box->get_database(d);
			if (database.is_null()) {
				continue;
			}
			// Always targets the SAME file a database already points at (if
			// it has one), overwriting it in place, so anything already
			// wired up to that .res path keeps working without needing to
			// be re-pointed. Only a database that has never been saved
			// before gets a fresh path, named after its box and its own
			// name so multiple databases never collide in one folder.
			String database_path = database->get_path();
			if (database_path.is_empty()) {
				const String database_name = database->get_name().is_empty() ? vformat("database%d", d + 1) : database->get_name();
				database_path = base_dir.path_join("databases")
										.path_join(box_name.to_snake_case() + "_" + database_name.to_snake_case() + ".res");
			}
			const Error error = ResourceSaver::get_singleton()->save(database, database_path);
			if (error != OK) {
				_log_line(vformat("Could not save \"%s / %s\" to %s (error %d).",
									box_name, database->get_name(), database_path, (int)error),
						Color(1, 0.5f, 0.4f));
			}
		}
	}
}

void MMDatabaseEditor::_on_save_pressed() {
	if (_database.is_null()) {
		_log_line("Nothing to save yet. Press Build database first.", Color(1, 0.85f, 0.4f));
		return;
	}
	if (_mm_resource.is_null()) {
		_log_line("Assign a Motion Matching Resource above first.", Color(1, 0.5f, 0.4f));
		return;
	}
	if (_mm_resource->get_path().is_empty()) {
		_log_line(
				"Save the Motion Matching Resource to disk first (from the Inspector), so there "
				"is a folder to save databases next to.",
				Color(1, 0.5f, 0.4f));
		return;
	}

	// Every box and every database gets saved, not just the one currently
	// selected -- otherwise switching to a different box before pressing
	// Save would silently skip whatever was built for the others.
	_save_all_databases();

	_mm_resource->set_database(_extra_database);
	const Error error = ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	if (error != OK) {
		_log_line(vformat("Save failed with error %d.", (int)error), Color(1, 0.5f, 0.4f));
		return;
	}

	_log_line("Saved.", Color(0.5f, 1.0f, 0.6f));
}

void MMDatabaseEditor::_on_clip_edited() {
	TreeItem *item = _clip_table->get_edited();
	if (item == nullptr) {
		return;
	}
	Dictionary settings = _clip_settings.get(item->get_text(0), Dictionary());
	settings["tags"] = item->get_text(2).to_int();
	settings["category"] = MMFeatureExtractor::guess_category_from_tags(settings["tags"]);
	_clip_settings[item->get_text(0)] = settings;
}

void MMDatabaseEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_on_resource_picked", "resource"), &MMDatabaseEditor::_on_resource_picked);
	ClassDB::bind_method(D_METHOD("_on_library_picked", "resource"), &MMDatabaseEditor::_on_library_picked);
	ClassDB::bind_method(D_METHOD("_on_skeleton_path_changed", "text"), &MMDatabaseEditor::_on_skeleton_path_changed);
	ClassDB::bind_method(D_METHOD("_on_skeleton_picker_pressed"), &MMDatabaseEditor::_on_skeleton_picker_pressed);
	ClassDB::bind_method(D_METHOD("_on_skeleton_popup_selected", "index"), &MMDatabaseEditor::_on_skeleton_popup_selected);
	ClassDB::bind_method(D_METHOD("_on_left_foot_override_changed", "text"), &MMDatabaseEditor::_on_left_foot_override_changed);
	ClassDB::bind_method(D_METHOD("_on_right_foot_override_changed", "text"), &MMDatabaseEditor::_on_right_foot_override_changed);
	ClassDB::bind_method(D_METHOD("_on_box_selected", "index"), &MMDatabaseEditor::_on_box_selected);
	ClassDB::bind_method(D_METHOD("_on_add_box_pressed"), &MMDatabaseEditor::_on_add_box_pressed);
	ClassDB::bind_method(D_METHOD("_on_box_name_changed", "text"), &MMDatabaseEditor::_on_box_name_changed);
	ClassDB::bind_method(D_METHOD("_on_database_selected", "index"), &MMDatabaseEditor::_on_database_selected);
	ClassDB::bind_method(D_METHOD("_on_add_database_pressed"), &MMDatabaseEditor::_on_add_database_pressed);
	ClassDB::bind_method(D_METHOD("_on_database_name_changed", "text"), &MMDatabaseEditor::_on_database_name_changed);
	ClassDB::bind_method(D_METHOD("_on_loop_toggle_toggled", "pressed"), &MMDatabaseEditor::_on_loop_toggle_toggled);
	ClassDB::bind_method(D_METHOD("_on_scan_pressed"), &MMDatabaseEditor::_on_scan_pressed);
	ClassDB::bind_method(D_METHOD("_on_build_pressed"), &MMDatabaseEditor::_on_build_pressed);
	ClassDB::bind_method(D_METHOD("_on_save_pressed"), &MMDatabaseEditor::_on_save_pressed);
	ClassDB::bind_method(D_METHOD("_on_validate_pressed"), &MMDatabaseEditor::_on_validate_pressed);
	ClassDB::bind_method(D_METHOD("_on_clip_edited"), &MMDatabaseEditor::_on_clip_edited);
}

#endif // TOOLS_ENABLED
