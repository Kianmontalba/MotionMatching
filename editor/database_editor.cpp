#ifdef TOOLS_ENABLED

#include "motion_matching_editor.hpp"
#include "animation_library.hpp"

#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

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

	_skeleton_path = memnew(LineEdit);
	_skeleton_path->set_text("%GeneralSkeleton");
	_skeleton_path->set_placeholder("Node path to the Skeleton3D in the open scene");
	mm_add_row(this, "Skeleton node path", _skeleton_path);

	// Backed by the resource above, not a typed-in path: picking (or
	// dropping) a library here writes it straight onto the Motion Matching
	// Resource and saves that resource to disk immediately, so it survives
	// closing the dock, switching scenes, or an addon reload -- none of
	// which this control's own state would otherwise survive.
	_library_picker = memnew(EditorResourcePicker);
	_library_picker->set_base_type("AnimationLibrary");
	mm_add_row(this, "Animation library", _library_picker);

	_sample_rate = memnew(SpinBox);
	_sample_rate->set_min(10);
	_sample_rate->set_max(120);
	_sample_rate->set_value(30);
	mm_add_row(this, "Sample rate (Hz)", _sample_rate);

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
	_database = _mm_resource->get_database();
	_schema = _mm_resource->get_schema();

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
	if (_schema.is_null()) {
		_schema = MMFeatureSchema::make_default();
	}

	Ref<MMFeatureExtractor> extractor;
	extractor.instantiate();
	extractor->set_schema(_schema);
	extractor->set_sample_rate((float)_sample_rate->get_value());

	_database = extractor->build_database(skeleton, _library, _clip_settings);
	_progress->set_value(1.0);

	if (_database.is_null()) {
		_log_line("Build failed.", Color(1, 0.5f, 0.4f));
		return;
	}

	const Dictionary stats = _database->get_statistics();
	_log_line(vformat("Built %s frames from %s clips, %s dimensions, %.1f MB of features.",
					  stats["frame_count"], stats["animation_count"], stats["dimension"],
					  (double)(int64_t)stats["feature_bytes"] / 1048576.0),
			Color(0.5f, 1.0f, 0.6f));
}

void MMDatabaseEditor::_on_save_pressed() {
	if (_database.is_null()) {
		_log_line("Nothing to save yet. Press Build database first.", Color(1, 0.85f, 0.4f));
		return;
	}
	if (_mm_resource.is_null()) {
		_log_line("Assign a Motion Matching Resource above first -- the database always saves "
				  "onto that resource's own database slot, never a separate guessed path.",
				Color(1, 0.5f, 0.4f));
		return;
	}

	// Always targets the SAME database this resource already points at (if
	// it has one), overwriting it in place, so anything already wired up to
	// that .res path -- a running scene, another resource referencing it --
	// keeps working against the freshly built data without needing to be
	// re-pointed. Only when there is no existing database yet is a new path
	// chosen, next to the Motion Matching Resource itself.
	String database_path;
	if (_mm_resource->get_database().is_valid() && !_mm_resource->get_database()->get_path().is_empty()) {
		database_path = _mm_resource->get_database()->get_path();
	} else if (!_mm_resource->get_path().is_empty()) {
		database_path = _mm_resource->get_path().get_base_dir().path_join("database.res");
	} else {
		_log_line(
				"Save the Motion Matching Resource to disk first (from the Inspector), so there "
				"is a folder to save the database next to.",
				Color(1, 0.5f, 0.4f));
		return;
	}

	_mm_resource->set_database(_database);

	const Error error = ResourceSaver::get_singleton()->save(_database, database_path);
	if (error != OK) {
		_log_line(vformat("Save failed with error %d.", (int)error), Color(1, 0.5f, 0.4f));
		return;
	}

	if (!_mm_resource->get_path().is_empty()) {
		ResourceSaver::get_singleton()->save(_mm_resource, _mm_resource->get_path());
	}

	_log_line("Saved " + database_path, Color(0.5f, 1.0f, 0.6f));
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
	ClassDB::bind_method(D_METHOD("_on_scan_pressed"), &MMDatabaseEditor::_on_scan_pressed);
	ClassDB::bind_method(D_METHOD("_on_build_pressed"), &MMDatabaseEditor::_on_build_pressed);
	ClassDB::bind_method(D_METHOD("_on_save_pressed"), &MMDatabaseEditor::_on_save_pressed);
	ClassDB::bind_method(D_METHOD("_on_validate_pressed"), &MMDatabaseEditor::_on_validate_pressed);
	ClassDB::bind_method(D_METHOD("_on_clip_edited"), &MMDatabaseEditor::_on_clip_edited);
}

#endif // TOOLS_ENABLED
