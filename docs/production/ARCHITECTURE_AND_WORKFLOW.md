# MOTION MATCHING — Architecture & Production Workflow

Production-addon documentation only — no demo content is referenced.
Evidence labels as in the preceding documents.

# Part A — Project Architecture

## Folder structure [SOURCE]
```
include/    35 headers — one class family per file, GDCLASS declarations
src/        matching .cpp implementations (two intentional splits:
            cache.hpp -> cache_manager.cpp (MMSearchCache) + thread_pool.cpp
            (MMSearchWorker); motion_matching.hpp -> motion_matching.cpp
            (MotionMatchingController) + motion_matching_database.cpp
            (MotionMatchingResource))
editor/     TOOLS_ENABLED-gated: one header declaring all 5 editor
            classes, one .cpp per class
docs/       reference documentation (this file and its companions)
```
*(The addon also has `godot-cpp/` as a vendored build dependency and
build files at the repository root — not part of the addon's own
runtime code.)*

## Module initialization [SOURCE]
1. GDExtension entry point (`motion_matching_library_init`) registers an
   initializer/terminator pair via `GDExtensionBinding::InitObject`.
2. At `MODULE_INITIALIZATION_LEVEL_SCENE`: 24 runtime classes registered,
   grouped conceptually as data layer → rig/clip analysis → build layer →
   search layer → motion layer → skeleton modifiers → runtime
   integration.
3. If the editor build (`TOOLS_ENABLED`) is active, at
   `MODULE_INITIALIZATION_LEVEL_EDITOR`: 4 editor UI classes registered,
   then the editor plugin is added.
4. Termination mirrors initialization: the editor plugin is removed
   first (if present), then the scene-level classes require no explicit
   unregistration (handled automatically by the GDExtension binding
   layer **[EXPECTED, standard Godot behavior]**).

## Class relationships (ownership vs. reference) [SOURCE]
```
MotionMatchingController (owns, by value or Ref<>)
 ├─ MMTrajectory, MMRootMotion   — always instantiated
 ├─ MMPoseSearch                — always instantiated
 ├─ MMSearchCache, MMSearchWorker — always instantiated (cache is a
 │                                  plain C++ member, not a GDCLASS)
 ├─ MMProfiler                  — always instantiated
 └─ MMTraversal, MMMotionWarp   — optional, null by default (opt-in)

MotionMatchingController (references, does not own)
 └─ MotionMatchingResource
      ├─ MotionMatchingDatabase (contains MMAnimationEntry per clip)
      ├─ MMFeatureSchema (references MMSkeletonProfile, optional)
      ├─ MMCostFunction
      └─ AnimationLibrary (a core Godot type)

Build-time only (not runtime-owned by the controller)
 MMFeatureExtractor
  ├─ uses MMPoseSampler, MMSkeletonProfile, MMClipAnalyzer, MMFeatureSchema
  └─ produces MotionMatchingDatabase

Skeleton-attached, driven by gameplay script, not owned by the controller
 MMFootIKModifier, MMAimIKModifier, MMWarpModifier
  └─ all use MMIKSolver internally

Editor-only
 MotionMatchingEditorPlugin hosts MMDatabaseEditor, MMFeatureEditor,
 MMTrajectoryEditor, MMDebugTools
```

## Runtime pipeline (per physics tick) [SOURCE]
Timers advance → trajectory update → optional traversal probe → playback
advance → async response poll (if enabled) → search (if due) → root
motion integration. See `SYSTEMS.md`'s "Motion Matching" entry for the
full breakdown, and the Search Pipeline diagram below for the search step
specifically.

## Search pipeline [SOURCE]
```
character intent (desired velocity/facing)
  -> MMTrajectory (critically-damped spring prediction)
  -> query vector (character-space, schema-defined layout)
  -> cache key = quantize(query) mixed with active filter bits
  -> MMSearchCache.lookup()
       hit  -> reconstruct match from cached (frame, cost)
       miss -> MMPoseSearch.search() [sync] or MMSearchWorker.submit() [async]
                 -> KD-tree descent, pruned via MMCostFunction's weight table
                 -> match result + search stats
  -> MMSearchCache.store()
  -> hysteresis check (cooldown, minimum improvement)
       yes -> apply match -> MMRootMotion.notify_frame_jump()
                           -> AnimationNodeMotionMatching blends toward
                              the new (clip, absolute time)
       no  -> keep playing the current clip
```

## Animation pipeline [SOURCE]
Matched (clip, time) → `AnimationNodeMotionMatching::_process_animation_node()`
blends the outgoing and incoming clip via `blend_animation()` with an
absolute time argument → `AnimationTree` presents the blended pose →
skeleton modifiers (`MMWarpModifier` → `MMAimIKModifier` → `MMFootIKModifier`,
in the order implied by each class's own doc comments) further adjust the
pose → final presented skeleton.

## Database generation pipeline [SOURCE]
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

## Thread model [SOURCE]
Exactly one background thread exists in the entire addon —
`MMSearchWorker`'s, owned by `MotionMatchingController` by value (not by
pointer/`Ref`), lifecycle tied directly to the controller's own. Started
by `rebuild()` (if async search is enabled), stopped by
`_notification(NOTIFICATION_EXIT_TREE)` and (as of a fix this project
made) also at the top of `set_resource()`, before any resource-owned
`Ref` can be reassigned out from under an in-flight search. No other
thread creation exists anywhere in the addon.

## Memory ownership [SOURCE]
- **`Ref<T>` (reference-counted)** is used for every `RefCounted`- and
  `Resource`-derived object throughout the addon — zero raw `new`/`delete`
  exist anywhere in the codebase.
- The **database's flat packed arrays** are allocated once at build time
  and never resized at runtime — `finalize()` is the point after which
  the data is treated as immutable.
- The **KD-tree** owns an index array into the database's existing data,
  not a copy of the feature vectors themselves.
- The **search worker** owns its own copy of the query vector (copied
  under a lock in `submit()`), specifically to avoid a data race on the
  controller's live query buffer.
- The one **verified ownership defect** in this project's history: a
  `MotionMatchingResource`-owned `Ref` (`_cost_function`) could be
  destroyed on the main thread while a raw pointer to it was still held
  by the background worker mid-search. This has been fixed by reordering
  `set_resource()`'s statements; see `CHANGELOG.md`.

---

# Part B — Complete Production Workflow

Each stage: Inputs / Outputs / Internal processing / Runtime behavior.

### 1. Rig Detection
**Inputs:** a `Skeleton3D`. **Outputs:** a resolved role→bone-name mapping,
a human-readable detection report, a list of any missing roles.
**Internal processing:** structural graph analysis (leg/arm/spine/head
chain identification from topology) → name-token matching (disambiguates
left/right, recognizes common conventions) → manual override (always
wins, persists across re-detection). **Runtime behavior:** one-time,
build/setup time only — not re-run at gameplay runtime.

### 2. Skeleton Profile
The saved output of Rig Detection (`MMSkeletonProfile` is the resource
that stores it). **Inputs:** none beyond what Rig Detection already
produced. **Outputs:** a reusable, saveable asset — `.tres` — that can be
reapplied to the same skeleton without rerunning detection, or manually
edited to fix any gaps.

### 3. Animation Import
**Inputs:** source animation files, imported by Godot's own standard
import pipeline into `Animation` resources inside an `AnimationLibrary`
(this step uses Godot's built-in importer, not addon-specific code —
**[EXPECTED]**, no custom import logic exists in this addon for this
stage). **Outputs:** an `AnimationLibrary` ready for analysis.

### 4. Clip Analysis
**Inputs:** each `Animation` in the library, the target `Skeleton3D`.
**Outputs:** measured `MMClipStats` (speed, turn rate, vertical range,
contact ratio, etc.) and a resulting tags bitmask + category per clip.
**Internal processing:** `MMClipAnalyzer::classify()` compares measured
stats (normalized by hip height) against configured thresholds;
`calibrate_speed_bands()` can derive those thresholds from the specific
library's own speed distribution first. **Runtime behavior:** build-time
only.

### 5. Feature Extraction
**Inputs:** `Skeleton3D`, `AnimationLibrary`, `MMFeatureSchema`,
`MMSkeletonProfile`, `MMClipAnalyzer`. **Outputs:** per-frame feature
vectors packed according to the schema's layout. **Internal processing:**
`MMPoseSampler` binds each clip once, samples at the configured rate;
each sample's pose/root/trajectory data is written into the schema-defined
offsets. **Runtime behavior:** build-time only.

### 6. Database Generation
**Inputs:** the extracted per-frame feature data from Feature Extraction.
**Outputs:** a `MotionMatchingDatabase` resource, `finalize()`d (dimension
locked, `format_version` stamped). **Runtime behavior:** the final
build-time step; from `finalize()` onward the data is immutable.

### 7. KD-Tree Build
**Inputs:** a finalized `MotionMatchingDatabase`. **Outputs:** a built
`MMPoseSearch` tree structure. **Internal processing:** recursive
widest-axis splitting. **Runtime behavior:** happens once per controller
`rebuild()` call — at `_ready()` and whenever the resource/database is
swapped — not per search.

### 8. Runtime Initialization
**Inputs:** a `MotionMatchingResource` assigned to a
`MotionMatchingController`. **Outputs:** a controller ready to search and
play. **Internal processing:** resolve character node → sync from
resource → bind `AnimationTree` → `rebuild()` (KD-tree build, cache clear,
worker start). **Runtime behavior:** `_ready()`, once per controller
instance (and again whenever `set_resource()` is called).

### 9. Query Building
**Inputs:** current trajectory prediction, current pose state.
**Outputs:** a query vector in the schema's layout, in character space.
**Internal processing:** `MMTrajectory::write_features()` fills the
trajectory block; pose/root data fills the rest. **Runtime behavior:**
once per search attempt (not necessarily every tick — gated by
`search_interval`).

### 10. Search
**Inputs:** the query vector, the active search filter, the cost
function. **Outputs:** a match result (frame, clip, time, cost) + search
stats. **Internal processing:** cache check first, then KD-tree descent
(sync or async) on a miss. **Runtime behavior:** once per search attempt.

### 11. Match Selection
**Inputs:** the search's match result, the current playback state.
**Outputs:** a decision — switch or keep playing. **Internal processing:**
hysteresis check (cooldown timer, minimum-cost-improvement threshold).
**Runtime behavior:** once per search attempt.

### 12. Blend
**Inputs:** the accepted match (new clip, new absolute time), the
currently-playing clip/time. **Outputs:** a blended pose. **Internal
processing:** `AnimationNodeMotionMatching` cross-fades both clips over a
configurable blend time, at the new clip's absolute matched time (not
zero). **Runtime behavior:** every tick, for the duration of the blend
window.

### 13. Root Motion
**Inputs:** the current (and, mid-blend, previous) frame's baked
velocity. **Outputs:** an accumulated `Transform3D` delta.
**Internal processing:** exponential smoothing + integration; immediate
adoption of the new velocity on the tick following a frame jump.
**Runtime behavior:** every tick.

### 14. Motion Warp *(optional stage)*
**Inputs:** an active warp window, the raw root delta. **Outputs:** a
corrected root delta. **Internal processing:** blends in the remaining
correction needed to reach the window's target by its end time.
**Runtime behavior:** every tick, only while a warp is active.

### 15. Foot IK *(optional stage, recommended for grounded characters)*
**Inputs:** database contact flags (via the controller's debug info),
ground raycast results. **Outputs:** adjusted leg/pelvis pose.
**Internal processing:** analytic two-bone solve per leg, pelvis height
compensation, contact-based locking. **Runtime behavior:** every tick,
after the animation pose is sampled.

### 16. Aim IK *(optional stage)*
**Inputs:** a gameplay-supplied target. **Outputs:** adjusted
spine/neck/head pose. **Internal processing:** weighted multi-bone
rotation toward the target, cone-limited. **Runtime behavior:** every
tick, after Warp Modifier's corrections (by design).

### 17. Traversal *(optional stage)*
**Inputs:** the predicted trajectory, physics world geometry.
**Outputs:** a detected obstacle type + points, fed into the next
search's filter and (typically) into a subsequent Motion Warp target.
**Runtime behavior:** once per tick, only when grounded and a traversal
instance is assigned.

### 18. AnimationTree Playback
**Inputs:** the blended pose from stage 12, further modified by any of
stages 14-17. **Outputs:** the final presented character animation.
**Runtime behavior:** continuous, every tick, for as long as the
character exists.
