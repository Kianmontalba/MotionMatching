#ifndef MM_EDITOR_HPP
#define MM_EDITOR_HPP

#ifdef TOOLS_ENABLED

#include "cost_function.hpp"
#include "feature.hpp"
#include "feature_extractor.hpp"
#include "frame_database.hpp"
#include "mm_types.hpp"
#include "motion_matching.hpp"

#include <godot_cpp/classes/box_container.hpp>
#include <godot_cpp/classes/button.hpp>
#include <godot_cpp/classes/check_box.hpp>
#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/editor_resource_picker.hpp>
#include <godot_cpp/classes/h_slider.hpp>
#include <godot_cpp/classes/label.hpp>
#include <godot_cpp/classes/line_edit.hpp>
#include <godot_cpp/classes/option_button.hpp>
#include <godot_cpp/classes/progress_bar.hpp>
#include <godot_cpp/classes/rich_text_label.hpp>
#include <godot_cpp/classes/spin_box.hpp>
#include <godot_cpp/classes/tab_container.hpp>
#include <godot_cpp/classes/tree.hpp>
#include <godot_cpp/classes/v_box_container.hpp>

namespace godot {

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
	LineEdit *_skeleton_path = nullptr;
	// The dock's anchor: whichever MotionMatchingResource the user is
	// preparing. Everything else here reads from and writes back to this
	// same resource (and, in turn, its own path on disk) instead of holding
	// its own copy of a path string, so nothing is lost if the dock's
	// controls are ever torn down and recreated (e.g. a GDExtension reload).
	EditorResourcePicker *_resource_picker = nullptr;
	EditorResourcePicker *_library_picker = nullptr;
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

	// Manual bone-role mapping, for rigs auto_detect() cannot read (unusual
	// naming, a non-humanoid skeleton, ...). Off (auto-detect on) by default;
	// switching it on reveals one dropdown per role, each populated from
	// whatever Skeleton3D is currently resolved.
	CheckBox *_auto_detect_profile = nullptr;
	Button *_load_bones_button = nullptr;
	Tree *_bone_mapping_tree = nullptr;
	PackedStringArray _bone_names_cache;

	void _on_load_bones_pressed();
	void _on_auto_detect_toggled(bool p_pressed);
	Ref<MMSkeletonProfile> _build_manual_profile() const;

	Ref<MotionMatchingResource> _mm_resource;
	Ref<AnimationLibrary> _library;
	Ref<MotionMatchingDatabase> _database;
	Ref<MMFeatureSchema> _schema;
	Dictionary _clip_settings;

	void _on_resource_picked(const Ref<Resource> &p_resource);
	void _on_library_picked(const Ref<Resource> &p_resource);
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
	TabContainer *_panel = nullptr;
	MMDatabaseEditor *_database_editor = nullptr;
	MMFeatureEditor *_feature_editor = nullptr;
	MMTrajectoryEditor *_trajectory_editor = nullptr;
	MMDebugTools *_debug_tools = nullptr;

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
