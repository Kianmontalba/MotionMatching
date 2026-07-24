# MOTION MATCHING — Project Architecture

All **[SOURCE]** unless marked otherwise. Complements `docs/architecture.md`
(the original 5-layer breakdown) with execution-order and pipeline detail.

## Folder structure
```
include/    35 headers — one class family per file, GDCLASS declarations
src/        matching .cpp implementations (some intentionally split:
            cache.hpp -> cache_manager.cpp (MMSearchCache) + thread_pool.cpp
            (MMSearchWorker); motion_matching.hpp -> motion_matching.cpp
            (MotionMatchingController) + motion_matching_database.cpp
            (MotionMatchingResource))
editor/     TOOLS_ENABLED-gated: one header (motion_matching_editor.hpp)
            declaring all 5 editor classes, one .cpp per class
demo/       minimal project wiring — currently missing project.godot,
            see PRE_RELEASE_REVIEW.md's RB-1
tests/      8 GDScript test scripts + a headless runner + synthetic
            skeleton helper — never executed in any session
docs/       this file plus architecture.md, workflow.md, api.md,
            optimization.md, HANDOFF_PACKAGE.md, PRE_RELEASE_REVIEW.md,
            CLASS_REFERENCE.md, SYSTEMS.md
godot-cpp/  vendored dependency (submodule) — gen/ currently empty in
            this sandbox due to an interrupted build attempt; unaffected
            on a real machine
```

## Class relationships (who owns / references whom)
```
MotionMatchingController (Node)
 ├─ owns: MMTrajectory, MMRootMotion, MMPoseSearch, MMSearchCache,
 │        MMSearchWorker, MMProfiler
 ├─ optionally references: MMTraversal, MMMotionWarp (opt-in, null default)
 ├─ reads: MotionMatchingResource
 │           ├─ references: MotionMatchingDatabase
 │           │                ├─ contains: MMAnimationEntry (one per clip)
 │           ├─ references: MMFeatureSchema
 │           │                ├─ references: MMSkeletonProfile (optional)
 │           ├─ references: MMCostFunction
 │           └─ references: AnimationLibrary (core Godot type)
 └─ binds (auto-discovered): AnimationNodeMotionMatching (inside an
                              AnimationTree), MMFootIKModifier /
                              MMAimIKModifier / MMWarpModifier (siblings
                              under the character's Skeleton3D, driven by
                              gameplay script, not owned by the controller)

MMFeatureExtractor (build-time only, not runtime-owned by anything)
 ├─ uses: MMPoseSampler, MMSkeletonProfile, MMClipAnalyzer, MMFeatureSchema
 └─ produces: MotionMatchingDatabase

Editor tools (TOOLS_ENABLED only)
 └─ MotionMatchingEditorPlugin hosts MMDatabaseEditor, MMFeatureEditor,
    MMTrajectoryEditor, MMDebugTools in one bottom panel
```

## Initialization order
1. `motion_matching_library_init()` (the `extern "C"` GDExtension entry
   point) constructs a `GDExtensionBinding::InitObject` and registers
   `initialize_motion_matching_module`/`uninitialize_motion_matching_module`.
2. At `MODULE_INITIALIZATION_LEVEL_SCENE`: 24 classes registered, in the
   order: data layer (`MMFeatureSchema`, `MMAnimationEntry`,
   `MotionMatchingDatabase`) → rig/clip analysis (`MMSkeletonProfile`,
   `MMClipAnalyzer`) → build layer (`MMPoseSampler`, `MMFeatureExtractor`,
   `MMAnimationLibraryTools`) → search layer (`MMCostFunction`,
   `MMPoseSearch`, `MMProfiler`) → motion layer (`MMTrajectory`,
   `MMRootMotion`, `MMTraversal`, `MMMotionWarp`) → skeleton modifiers
   (`MMWarpModifier`, `MMIKSolver`, `MMFootIKModifier`, `MMAimIKModifier`)
   → runtime (`MotionMatchingResource`, `MotionMatchingController`,
   `AnimationNodeMotionMatching`, `MMDebugDraw`).
3. If `TOOLS_ENABLED`, at `MODULE_INITIALIZATION_LEVEL_EDITOR`: 4 editor
   classes registered, then `EditorPlugins::add_by_type<MotionMatchingEditorPlugin>()`.
4. Per-instance: `MotionMatchingController::_ready()` — early-return (with
   processing disabled) if `Engine::is_editor_hint()`; otherwise resolve
   the character node, sync from the assigned resource
   (`_sync_from_resource()`), bind any sibling `AnimationTree`
   (`_bind_animation_tree()`), `rebuild()` (build the KD-tree, clear the
   cache, start the async worker if enabled), then enable physics
   processing.

## Runtime update order (per `_physics_process()` tick)
1. Timers advance (`_time_since_search`, `_time_in_clip`,
   `_switch_cooldown_timer`, `_landing_timer`, `_category_lock_timer`,
   `_time_since_grounded`).
2. `MMTrajectory::update()` — spring-integrates predicted future path from
   current state + desired velocity/facing.
3. `_evaluate_traversal()` — opt-in obstacle probe (only if a traversal
   instance is assigned and the character is grounded).
4. `_advance_playback()` — steps clip time forward, applies blend weight
   between outgoing/incoming clips.
5. Async response poll (if async search enabled) — consumes a finished
   background search result if the worker has one ready, feeds
   `MMProfiler::record()`.
6. If a search is due (`search_interval` elapsed, or forced): `_run_search()`
   — builds the query, evaluates continuation cost, builds the search
   filter (category lock → traversal bias → jump/airborne → landing →
   default grounded, in that priority order), checks the cache, falls
   through to sync or async KD-tree search on a miss.
7. `MMRootMotion::update()` — integrates root displacement from the
   current (and, mid-blend, previous) frame's velocity.

## Search pipeline (detail)
```
character intent (desired velocity/facing)
  -> MMTrajectory (critically-damped spring prediction)
  -> query vector (character-space, schema-defined layout)
  -> cache key = quantize(query) mixed with active filter bits
  -> MMSearchCache.lookup()
       hit  -> reconstruct MMMatchResult from cached (frame, cost)
       miss -> MMPoseSearch.search() [sync] or MMSearchWorker.submit() [async]
                 -> KD-tree descent, pruned via MMCostFunction's weight table
                 -> MMMatchResult + MMSearchStats
  -> MMSearchCache.store()
  -> _should_switch() (hysteresis: cooldown, minimum improvement)
       yes -> _apply_match() -> MMRootMotion.notify_frame_jump()
                              -> AnimationNodeMotionMatching blends toward
                                 the new (clip, absolute time)
       no  -> keep playing the current clip
```

## Database generation pipeline (detail, build-time / editor-time)
```
AnimationLibrary + Skeleton3D
  -> MMSkeletonProfile.auto_detect() (structure -> names -> manual override)
  -> MMFeatureSchema.apply_skeleton_profile() (fills pose bone list)
  -> per clip:
       MMPoseSampler.bind() (resolve tracks once)
       -> sample at MMFeatureExtractor.sample_rate
       -> MMClipAnalyzer.classify() (measured MMClipStats -> tags/category)
       -> pack into MotionMatchingDatabase via schema's layout
  -> MotionMatchingDatabase.finalize(dimension)
       -> stamps format_version, locks frame count
  -> save as .tres, assign to MotionMatchingResource.database
```

## Thread model
Exactly one background thread exists in the entire addon:
`MMSearchWorker`'s, owned by `MotionMatchingController` (by value, not by
pointer/Ref), lifecycle tied directly to the controller's own lifetime.
Started by `rebuild()` (if `async_search` is enabled), stopped by
`_notification(NOTIFICATION_EXIT_TREE)`, and (as of this session's fix)
also stopped at the top of `set_resource()` before any resource-owned
`Ref` can be reassigned/dropped out from under an in-flight search. No
other thread creation exists anywhere in the codebase — confirmed by
source inspection of every `.cpp` file touched or reviewed across this
project's sessions, not an exhaustive re-grep of all 35 files in this
specific pass.
