# MOTION MATCHING — API Reference

This covers the classes a game script normally touches. For internal
subsystems (search, cache, cost function) see `architecture.md`.

---

## MotionMatchingController (`Node`)

The node your character script drives every frame.

### Setup

| Property | Type | Description |
|---|---|---|
| `resource` | `MotionMatchingResource` | Bundles database, schema, cost function, and settings. |
| `character_path` | `NodePath` | Optional; used to read the character's transform for facing/root motion. |
| `animation_tree_path` | `NodePath` | The `AnimationTree` containing an `AnimationNodeMotionMatching`. Auto-bound at `_ready()` if left empty and one exists as a sibling. |

### Intent API (call every physics frame)

| Method | Description |
|---|---|
| `set_velocity(Vector3)` | Current measured velocity, in world space. |
| `set_desired_velocity(Vector3)` | Where the player/AI wants to go — this drives trajectory prediction. |
| `set_facing(Vector3)` | Desired facing direction, independent of movement direction (useful for strafing). |
| `set_direction(Vector3)` | Shorthand: sets facing from a single direction vector. |
| `set_ground_state(bool)` | Whether the character is grounded this frame. |
| `set_fall_distance(float)` | How far the character has fallen, for landing-clip selection. |
| `request_jump()` | Signals a jump; the next search is biased toward `MM_TAG_JUMP`. |
| `set_trajectory(PackedVector3Array positions, PackedVector3Array directions)` | Supply a fully custom predicted trajectory instead of the built-in spring predictor. |

### Gameplay filtering

| Method | Description |
|---|---|
| `set_required_tags(int mask)` | Only frames with **all** of these `MMTag` bits are eligible. |
| `set_blocked_tags(int mask)` | Frames with **any** of these bits are excluded. |
| `set_category_mask(int mask)` | Restrict to one or more `MMCategory` values (locomotion, airborne, traversal, combat, ...). |

### Readback

| Method | Description |
|---|---|
| `get_blend_weight()` | Current crossfade weight between outgoing and incoming clip. |
| `get_current_frame()` | Frame index into the matched clip. |
| `get_current_animation_id()` | Database id of the currently playing clip. |
| `get_root_motion()` | The `MMRootMotion` instance driving root displacement. |
| `get_trajectory()` | The `MMTrajectory` instance, useful for debug drawing. |
| `get_debug_info()` | Dictionary of match/search/profiler stats — see below. |
| `get_profiler()` | The `MMProfiler` instance (always present); `get_profiler().get_report()` for a raw dictionary. |

### Optional subsystems (opt-in by assignment; null/unassigned costs nothing)

| Property / Method | Description |
|---|---|
| `traversal` (`MMTraversal`) | Assigning one makes the controller probe the predicted trajectory for obstacles once per grounded tick and bias the next search's category/tags toward what it finds. Emits `traversal_requested(type, target)`. |
| `motion_warp` (`MMMotionWarp`) | Assigning one lets `begin_warp(target_transform)` / `end_warp()` / `is_warping()` drive root motion correction toward a target through `consume_root_motion()`. `begin_warp()` opens a single window spanning the rest of the currently playing clip; add custom windows via `get_motion_warp().add_window()` for finer control. |

`get_debug_info()` additionally exposes `cache_hits` / `cache_misses` (real
numbers once `MMSearchCache` is actually consulted during search),
`query_build_usec` / `continuation_eval_usec` / `switch_apply_usec` /
`update_total_usec` (per-phase timings), `profiler` (the full `MMProfiler`
report dictionary), and `traversal_active` / `traversal_type` / `warp_active`.

---

## MotionMatchingResource (`Resource`)

A `.tres` bundling everything the controller needs, so a character variant
is one resource swap.

| Property | Type |
|---|---|
| `animation_library` | `AnimationLibrary` |
| `database` | `MotionMatchingDatabase` |
| `schema` | `MMFeatureSchema` |
| `cost_function` | `MMCostFunction` |

`validate()` returns an `Array` of `{severity, clip, message}` Dictionaries
(the same shape `MMAnimationLibraryTools.validate_library()` uses) — call it
once, typically from the editor or `_ready()`, to check for a missing
database/library, a zero-dimension or empty database, or a schema/database
dimension mismatch before relying on it at runtime. An empty array means no
structural issues were found; it does not by itself guarantee good matches.

`MotionMatchingDatabase.format_version` is stamped automatically by
`finalize()`; `is_format_compatible()` reports whether a loaded database
matches what the current addon build expects, useful for catching a
database built by a different addon version.

---

## MMSkeletonProfile (`Resource`)

| Method | Description |
|---|---|
| `auto_detect(Skeleton3D)` | Runs the three-stage detection (structure → names → overrides). Returns `false` only if the skeleton has too few bones to be a biped. |
| `set_bone_name(role, name)` / `get_bone_name(role)` | Manual override / lookup for a single `MMBoneRole`. |
| `has_role(role)` | Whether detection found that role. |
| `get_missing_roles()` | Roles detection could not resolve. |
| `get_detection_report()` | Human-readable summary, shown in the Database editor tab. |
| `swap_sides()` | Swaps every left/right role pair — for mirrored rigs. |

See `include/skeleton_profile.hpp` for the full `MMBoneRole` enum (22 roles:
root, pelvis, spine, chest, neck, head, both legs, both arms).

---

## MMClipAnalyzer (`Resource`)

| Method | Description |
|---|---|
| `classify(MMClipStats, name)` | Returns an `MMTag` bitmask for one clip. |
| `category_for_tags(tags)` | Maps a tag mask to an `MMCategory`. |
| `calibrate_speed_bands(speeds)` | Derives walk/jog/run/sprint thresholds from a library's own speed distribution. Call this once after scanning a new library before relying on default thresholds. |
| `name_rules` | Optional `Dictionary` of pattern → tag-mask, for semantic tags motion can't reveal (weapon type, attack phase). Empty and unused unless `use_name_rules` is set. |

---

## MMFeatureExtractor (`RefCounted`)

| Method | Description |
|---|---|
| `set_schema(MMFeatureSchema)` | Layout to extract into. |
| `set_profile(MMSkeletonProfile)` | Skeleton to resolve bones against; auto-created if unset. |
| `set_analyzer(MMClipAnalyzer)` | Classifier to use; auto-created if unset. |
| `analyze_library(skeleton, library)` | Dry-run: returns per-clip stats and tags without building a database, for the editor's preview table. |
| `build_database(skeleton, library)` | Full build: samples every clip and returns a `MotionMatchingDatabase`. |
| `append_animation(database, skeleton, animation, name)` | Adds a single clip to an existing database — used for incremental library growth. |

---

## Tag and Category Reference

Tags (`MMTag`, from `mm_types.hpp`) are a 32-bit mask; a frame can carry
several at once (e.g. `MM_TAG_RUN | MM_TAG_STRAFE`):

```
Locomotion: IDLE, WALK, JOG, RUN, SPRINT, CROUCH, PRONE, STRAFE, TURN, PIVOT, START, STOP
Airborne:   JUMP, FALL, LAND
Traversal:  VAULT, MANTLE, CLIMB, SLIDE, ROLL
Combat:     ATTACK, RELOAD, HIT, AIM, UNARMED, RIFLE, PISTOL
Other:      MIRRORED, USER_0..USER_2 (reserved for game-specific tags)
```

Categories (`MMCategory`) are the coarser, mutually-exclusive filter applied
before tag matching: `LOCOMOTION`, `AIRBORNE`, `TRAVERSAL`, `COMBAT`,
`INTERACTION`, `CUSTOM`.

Convenience masks are predefined for the common cases: `MM_TAG_MASK_GROUNDED`,
`MM_TAG_MASK_AIRBORNE`, `MM_TAG_MASK_TRAVERSAL`.
