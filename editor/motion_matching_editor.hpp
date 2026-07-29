#ifndef MM_EDITOR_HPP
#define MM_EDITOR_HPP

#ifdef TOOLS_ENABLED

#include "cost_function.hpp"
#include "feature.hpp"
#include "feature_extractor.hpp"
#include "frame_database.hpp"
#include "mm_box_animation.hpp"
#include "mm_extra_database.hpp"
#include "mm_types.hpp"
#include "motion_matching.hpp"

#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/check_button.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_resource_picker.hpp>
#include <godot_cpp/classes/h_box_container.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/h_split_container.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/panel_container.hpp>
#include <godot_cpp/classes/popup_menu.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/scroll_container.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/split_container.hpp>
#include <godot_cpp/classes/tab_container.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>
#include <godot_cpp/classes/window.hpp>

namespace godot {

// ---------------------------------------------------------------------------
// MMNodePathField
//
// A LineEdit that also accepts a node dragged straight from the Scene dock --
// dropping a Skeleton3D onto it is both faster and less error prone than
// typing its path by hand. Typing still works exactly as before; this only
// adds the drop target on top.
// ---------------------------------------------------------------------------
class MMNodePathField : public LineEdit {
	GDCLASS(MMNodePathField, LineEdit);

protected:
	static void _bind_methods();

public:
	bool _can_drop_data(const Vector2 &p_point, const Variant &p_data) const override;
	void _drop_data(const Vector2 &p_point, const Variant &p_data) override;
};

// ---------------------------------------------------------------------------
// MMDatabaseEditor
//
// Build, inspect and save a motion database. The clip table is the important
// part: with 800 clips, tagging is the job, and doing it in the inspector one
// resource at a time is not a workflow.
// ---------------------------------------------------------------------------
class MMDatabaseEditor : public VBoxContainer {
	GDCLASS(MMDatabaseEditor, VBoxContainer);

private:
	// Compact two-column layout: settings live in a narrow left column with
	// their own independent scroll, the clip table takes the wide right
	// column. Splitting the scroll this way means adding a new setting field
	// later only grows _left_stack inside _left_scroll -- it never grows the
	// dock itself, so the pinned action row / progress bar / log below it can
	// never get pushed off the bottom of the screen.
	HSplitContainer *_split = nullptr;
	VBoxContainer *_left_root = nullptr;
	ScrollContainer *_left_scroll = nullptr;
	VBoxContainer *_left_stack = nullptr;
	VBoxContainer *_right_root = nullptr;

	// Wraps a single field as its own small labeled box, matching the
	// "one setting per box, stacked" layout instead of one full-width row
	// per field.
	PanelContainer *_add_compact_box(VBoxContainer *p_parent, const String &p_label, Control *p_control);

	MMNodePathField *_skeleton_path = nullptr;
	// Lets the user browse every Skeleton3D actually present in the current
	// scene instead of typing a path or having to drag the node in from the
	// Scene dock. Pressing it fills _skeleton_popup with one entry per
	// Skeleton3D found, and picking one writes straight into _skeleton_path
	// (same as typing or dropping would).
	Button *_skeleton_picker_button = nullptr;
	PopupMenu *_skeleton_popup = nullptr;
	PackedStringArray _skeleton_candidates;

	// Two optional manual overrides -- everything else always comes from
	// auto-detect. Left empty (the common case), auto-detect decides these
	// same as any other role. Filled in, MMSkeletonProfile locks just these
	// two and keeps them through every future auto-detect pass, which is
	// what makes them survive a rebuild instead of resetting.
	LineEdit *_left_foot_override = nullptr;
	LineEdit *_right_foot_override = nullptr;
	void _on_left_foot_override_changed(const String &p_text);
	void _on_right_foot_override_changed(const String &p_text);
	// The dock's anchor: whichever MotionMatchingResource the user is
	// preparing. Everything else here reads from and writes back to this
	// same resource (and, in turn, its own path on disk) instead of holding
	// its own copy of a path string, so nothing is lost if the dock's
	// controls are ever torn down and recreated (e.g. a GDExtension reload).
	EditorResourcePicker *_resource_picker = nullptr;
	EditorResourcePicker *_library_picker = nullptr;

	// Animation-set hierarchy: MotionMatchingResource -> MMExtraDatabase (add
	// only) -> MMBoxAnimation (add only, renameable -- "Normal Rifle",
	// "Pistol", "Zombie") -> MotionMatchingDatabase (renameable -- "Walk",
	// "Sprint", "Jump", "Traversal"). Unlimited boxes, unlimited databases
	// per box. Exactly one box/database pair is "active" (what Build,
	// Validate, Save and the clip table below all operate on) at a time.
	Ref<MMExtraDatabase> _extra_database;
	OptionButton *_box_option = nullptr;
	LineEdit *_box_name_edit = nullptr;
	Button *_add_box_button = nullptr;
	OptionButton *_database_option = nullptr;
	LineEdit *_database_name_edit = nullptr;
	Button *_add_database_button = nullptr;
	// Not a status readout -- pressing it sets loop on every MMAnimationEntry
	// in the currently selected database in one go (handled inside
	// MotionMatchingDatabase itself), instead of clicking through the clip
	// table one row at a time.
	CheckButton *_loop_toggle = nullptr;

	// Script-facing grouping tag for the currently selected database (see
	// MotionMatchingDatabase::tag). Integer-only by construction (SpinBox,
	// not LineEdit -- there is no text to type, so no letters are possible),
	// range 1-100, baked onto the database at Build time. Several
	// differently-named databases can share one tag; play_by_tag() is what
	// reads it back at runtime.
	SpinBox *_database_tag = nullptr;
	void _on_database_tag_changed(double p_value);

	void _on_box_selected(int p_index);
	void _on_add_box_pressed();
	void _on_box_name_changed(const String &p_text);
	void _on_database_selected(int p_index);
	void _on_add_database_pressed();
	void _on_database_name_changed(const String &p_text);
	void _on_loop_toggle_toggled(bool p_pressed);
	void _refresh_box_options();
	void _refresh_database_options();
	void _persist_extra_database();
	void _save_all_databases();

	// Collapsed by default -- these four fields are set once per box/database
	// and rarely revisited, so keeping them out of view is what keeps the
	// dock compact instead of every field always taking up space.
	Button *_advanced_toggle = nullptr;
	VBoxContainer *_advanced_section = nullptr;
	void _on_advanced_toggle_pressed();

	SpinBox *_sample_rate = nullptr;
	// Corrects a root/hip bone rest orientation that does not point the way
	// the character actually walks (common on Mixamo-style rigs, usually
	// off by exactly 180). Applied to the extractor at build time, same as
	// sample rate.
	SpinBox *_root_yaw_offset = nullptr;
	Button *_scan_button = nullptr;
	Button *_build_button = nullptr;
	Button *_save_button = nullptr;
	Button *_validate_button = nullptr;
	ProgressBar *_progress = nullptr;
	Tree *_clip_table = nullptr;
	RichTextLabel *_log = nullptr;

	void _on_skeleton_picker_pressed();
	void _on_skeleton_popup_selected(int p_index);
	void _collect_skeletons(Node *p_node, Node *p_scene_root);

	Ref<MotionMatchingResource> _mm_resource;
	Ref<AnimationLibrary> _library;
	Ref<MotionMatchingDatabase> _database;
	Ref<MMFeatureSchema> _schema;
	Dictionary _clip_settings;

	void _on_resource_picked(const Ref<Resource> &p_resource);
	void _on_library_picked(const Ref<Resource> &p_resource);
	void _on_skeleton_path_changed(const String &p_text);
	void _on_scan_pressed();
	void _on_build_pressed();
	void _on_save_pressed();
	void _on_validate_pressed();
	void _on_clip_edited();
	void _populate_table();
	void _log_line(const String &p_text, const Color &p_color = Color(1, 1, 1));
	Skeleton3D *_resolve_skeleton();

protected:
	static void _bind_methods();

public:
	MMDatabaseEditor();

	void set_schema(const Ref<MMFeatureSchema> &p_schema) { _schema = p_schema; }
	Ref<MotionMatchingDatabase> get_database() const { return _database; }
	void _ready() override;
};

// ---------------------------------------------------------------------------
// MMFeatureEditor
//
// Schema layout and cost weights. The weight sliders are normalized to a
// percentage of total cost so the numbers mean what a designer expects.
// ---------------------------------------------------------------------------
class MMFeatureEditor : public VBoxContainer {
	GDCLASS(MMFeatureEditor, VBoxContainer);

private:
	LineEdit *_trajectory_times = nullptr;
	LineEdit *_pose_bones = nullptr;
	LineEdit *_root_bone = nullptr;
	CheckBox *_include_bone_velocity = nullptr;
	CheckBox *_include_root_velocity = nullptr;
	Label *_dimension_label = nullptr;
	HSlider *_weights[MM_GROUP_MAX] = { nullptr };
	Label *_weight_labels[MM_GROUP_MAX] = { nullptr };
	Button *_apply_button = nullptr;

	Ref<MMFeatureSchema> _schema;
	Ref<MMCostFunction> _cost_function;

	void _on_apply_pressed();
	void _on_weight_changed(float p_value);
	void _refresh();

protected:
	static void _bind_methods();

public:
	MMFeatureEditor();

	void set_schema(const Ref<MMFeatureSchema> &p_schema);
	Ref<MMFeatureSchema> get_schema() const { return _schema; }
	Ref<MMCostFunction> get_cost_function() const { return _cost_function; }
};

// ---------------------------------------------------------------------------
// MMTrajectoryEditor
//
// Tunes the predictor and explains what each halflife does to the feel, which
// is the parameter set most likely to be tuned by someone who did not write
// the code.
// ---------------------------------------------------------------------------
class MMTrajectoryEditor : public VBoxContainer {
	GDCLASS(MMTrajectoryEditor, VBoxContainer);

private:
	SpinBox *_halflife_position = nullptr;
	SpinBox *_halflife_direction = nullptr;
	SpinBox *_max_speed = nullptr;
	SpinBox *_prediction_step = nullptr;
	RichTextLabel *_preview = nullptr;
	Ref<MMTrajectory> _trajectory;

	void _on_value_changed(double p_value);
	void _refresh_preview();

protected:
	static void _bind_methods();

public:
	MMTrajectoryEditor();
	Ref<MMTrajectory> get_trajectory() const { return _trajectory; }
};

// ---------------------------------------------------------------------------
// MMDebugTools
//
// Live profiler for the selected controller while the game runs.
// ---------------------------------------------------------------------------
class MMDebugTools : public VBoxContainer {
	GDCLASS(MMDebugTools, VBoxContainer);

private:
	LineEdit *_controller_path = nullptr;
	RichTextLabel *_readout = nullptr;
	Button *_refresh_button = nullptr;
	float _timer = 0.0f;

	void _on_refresh_pressed();

protected:
	static void _bind_methods();

public:
	MMDebugTools();
	void _process(double p_delta) override;
};

// ---------------------------------------------------------------------------
// MotionMatchingEditorPlugin
//
// Adds one bottom panel holding every tool, so the whole workflow lives in one
// place instead of across four inspectors.
// ---------------------------------------------------------------------------
class MotionMatchingEditorPlugin : public EditorPlugin {
	GDCLASS(MotionMatchingEditorPlugin, EditorPlugin);

private:
	// _root holds a one-row toolbar (just the Full Screen button) above
	// _panel, and is what actually gets docked to the bottom panel. _panel
	// itself is what moves into _fullscreen_window and back, since that is
	// the part that was feeling cramped, not the toolbar.
	VBoxContainer *_root = nullptr;
	TabContainer *_panel = nullptr;
	Button *_fullscreen_button = nullptr;
	// The toggle button add_control_to_bottom_panel() hands back for the
	// "Motion Matching" tab in the editor's bottom bar. Listened to
	// directly so pressing that tab jumps straight to full screen instead
	// of showing the cramped docked view first and waiting for a second
	// press on _fullscreen_button.
	Button *_bottom_panel_button = nullptr;
	// Created the first time Full Screen is pressed, then reused; not the
	// same as fullscreen video mode -- this is a large, ordinary, OS
	// decorated Window, so its own native close button is the "X" that
	// sends the dock back to the editor.
	Window *_fullscreen_window = nullptr;
	MMDatabaseEditor *_database_editor = nullptr;
	MMFeatureEditor *_feature_editor = nullptr;
	MMTrajectoryEditor *_trajectory_editor = nullptr;
	MMDebugTools *_debug_tools = nullptr;

	void _on_fullscreen_pressed();
	void _on_fullscreen_window_close_requested();
	void _on_bottom_panel_toggled(bool p_visible);

protected:
	static void _bind_methods();

public:
	MotionMatchingEditorPlugin();

	String _get_plugin_name() const override;
	void _enter_tree() override;
	void _exit_tree() override;
};

} // namespace godot

#endif // TOOLS_ENABLED
#endif // MM_EDITOR_HPP
