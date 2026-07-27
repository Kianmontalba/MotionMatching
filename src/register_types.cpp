#include "register_types.h"

#include "animation_library.hpp"
#include "animation_node_motion_matching.hpp"
#include "clip_analysis.hpp"
#include "cost_function.hpp"
#include "debug.hpp"
#include "feature.hpp"
#include "feature_extractor.hpp"
#include "frame_database.hpp"
#include "ik_system.hpp"
#include "mm_box_animation.hpp"
#include "mm_extra_database.hpp"
#include "motion_matching.hpp"
#include "motion_warping.hpp"
#include "pose_search.hpp"
#include "profiler.hpp"
#include "root_motion.hpp"
#include "skeleton_profile.hpp"
#include "trajectory.hpp"
#include "traversal.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#ifdef TOOLS_ENABLED
#include "../editor/motion_matching_editor.hpp"
#include <godot_cpp/classes/editor_plugin_registration.hpp>
#endif

using namespace godot;

void initialize_motion_matching_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Data layer.
		GDREGISTER_CLASS(MMFeatureSchema);
		GDREGISTER_CLASS(MMAnimationEntry);
		GDREGISTER_CLASS(MotionMatchingDatabase);
		GDREGISTER_CLASS(MMBoxAnimation);
		GDREGISTER_CLASS(MMExtraDatabase);

		// Rig and clip analysis.
		GDREGISTER_CLASS(MMSkeletonProfile);
		GDREGISTER_CLASS(MMClipAnalyzer);

		// Build layer.
		GDREGISTER_CLASS(MMPoseSampler);
		GDREGISTER_CLASS(MMFeatureExtractor);
		GDREGISTER_CLASS(MMAnimationLibraryTools);

		// Search layer.
		GDREGISTER_CLASS(MMCostFunction);
		GDREGISTER_CLASS(MMPoseSearch);
		GDREGISTER_CLASS(MMProfiler);

		// Motion layer.
		GDREGISTER_CLASS(MMTrajectory);
		GDREGISTER_CLASS(MMRootMotion);
		GDREGISTER_CLASS(MMTraversal);
		GDREGISTER_CLASS(MMMotionWarp);

		// Skeleton modifiers.
		GDREGISTER_CLASS(MMWarpModifier);
		GDREGISTER_CLASS(MMIKSolver);
		GDREGISTER_CLASS(MMFootIKModifier);
		GDREGISTER_CLASS(MMAimIKModifier);

		// Runtime.
		GDREGISTER_CLASS(MotionMatchingResource);
		GDREGISTER_CLASS(MotionMatchingController);
		GDREGISTER_CLASS(AnimationNodeMotionMatching);
		GDREGISTER_CLASS(MMDebugDraw);
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(MMNodePathField);
		GDREGISTER_CLASS(MMDatabaseEditor);
		GDREGISTER_CLASS(MMFeatureEditor);
		GDREGISTER_CLASS(MMTrajectoryEditor);
		GDREGISTER_CLASS(MMDebugTools);
		GDREGISTER_CLASS(MotionMatchingEditorPlugin);
		EditorPlugins::add_by_type<MotionMatchingEditorPlugin>();
	}
#endif
}

void uninitialize_motion_matching_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::remove_by_type<MotionMatchingEditorPlugin>();
	}
#endif
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}

extern "C" {
// Entry point declared in motion_matching.gdextension.
GDExtensionBool GDE_EXPORT motion_matching_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
		const GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_object(p_get_proc_address, p_library, r_initialization);

	init_object.register_initializer(initialize_motion_matching_module);
	init_object.register_terminator(uninitialize_motion_matching_module);
	init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_object.init();
}
}
