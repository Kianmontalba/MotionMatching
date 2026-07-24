# Changelog

All notable changes to this project are documented in this file. Format
loosely follows [Keep a Changelog](https://keepachangelog.com/).

## [v0.1.0-alpha] — Internal Alpha Freeze

**This is an internal alpha, not a public release.** It exists so the
developer can compile and run this addon for the first time. No item in
this changelog has been confirmed by an actual build or execution unless
explicitly stated otherwise — everything below describes what the source
code implements, reconstructed from development-session records.

### Currently Implemented Systems
Trajectory prediction, KD-tree pose search, cost-function-driven
matching, hysteresis-gated clip switching, root motion integration,
universal rig detection (`MMSkeletonProfile`), motion-measured clip
classification (`MMClipAnalyzer`), feature extraction/database build
pipeline, search result cache, background async search worker,
search-time profiler, foot IK, aim IK, pose-level warp modifier
(orientation/stride/lean), `AnimationTree` integration
(`AnimationNodeMotionMatching`), structured resource validation, database
format versioning, and the full editor tool suite (database builder,
feature/cost editor, trajectory tuner, live debug readout).

### Experimental Systems
- **`MMTraversal`** (obstacle detection via raycasting) — newly wired
  into the controller this cycle; zero runtime verification.
- **`MMMotionWarp`** (root-motion target correction) — newly wired this
  cycle; one explicitly unresolved design question (a clip switch
  occurring mid-warp is not handled automatically); zero runtime
  verification.

### Future Work
- Confirm or refute a `-ffast-math`/`MM_INFINITY` release-build risk with
  real before/after data.
- Parallelize the per-clip database build loop.
- Make search cache size/quantization step resource-tunable.
- Resolve the mid-warp clip-switch edge case in `MMMotionWarp`.
- Wire up or remove `MMPlaybackMode` (currently declared, registered, and
  unused).
- Bind the missing `MM_TAG_USER_0`/`_1`/`_2`/`MM_TAG_NONE` GDScript
  constants.
- Fix or remove the `search_completed` signal (declared, never emitted).
- Rename 3 boolean accessors to match the codebase's own `is_X`/`has_X`
  convention.
- Source at least one real animation library for compatibility testing
  (none currently exists in this repository — only a synthetic test
  skeleton used by the unit test suite).

### Fixed
- **`MotionMatchingController::set_resource()`** — closed a verified
  use-after-free race: the old code path reassigned resource-owned `Ref`s
  (`_cost_function`, `_database`, `_schema`) via `_sync_from_resource()`
  *before* `rebuild()` stopped the async search worker, so a worker thread
  mid-search could be left holding a raw pointer into an object the main
  thread had just destroyed. `_worker.stop()` now runs first.
- **`MMRootMotion::notify_frame_jump()`** — fixed dead state: the function
  wrote `_blend_linear`/`_blend_angular` but `update()` never read them, so
  a motion-matching switch was silently still eased in over
  `blend_halflife` instead of adopting the new clip's velocity
  immediately, contradicting the function's own documented intent. Added a
  `_jump_pending` flag, consumed on the next tick.
- **`MMFeatureExtractor::guess_tags_from_name()` /
  `guess_category_from_tags()`** — restored after a regression from an
  earlier rig-detection consolidation had left them called from
  `animation_library.hpp`/`database_editor.cpp` but declared nowhere
  (compile-breaking).
- **`MotionMatchingController::get_debug_info()`** — foot contact flags
  (`left_foot_contact`/`right_foot_contact`) were never exposed, so
  `MMFootIKModifier`'s foot-locking always received `false, false`
  regardless of real per-frame contact data baked into the database.

### Added
- **`MMSearchCache` integration** — `lookup()`/`store()` are now actually
  called from `_run_search()`/`_consume_result()`; the cache key mixes the
  quantized query with the active search filter so a gameplay-state change
  never reuses a stale answer.
- **`MMProfiler` integration** — instantiated automatically, fed on every
  real (non-cached) search and every accepted clip switch, surfaced via
  `get_debug_info()["profiler"]`. Four additional phase timings
  (`query_build_usec`, `continuation_eval_usec`, `switch_apply_usec`,
  `update_total_usec`) added alongside it.
- **`MMTraversal` integration** — opt-in; probes for obstacles once per
  grounded tick, emits the previously-unfired `traversal_requested`
  signal, biases the next search's category/tags.
- **`MMMotionWarp` integration** — opt-in; `begin_warp()`/`end_warp()`/
  `is_warping()` added to the controller, `consume_root_motion()` routes
  through the warp when active.
- **`MotionMatchingResource::validate()`** — structured pre-flight check
  (missing library, missing/empty/zero-dimension database, schema/database
  dimension mismatch, format incompatibility), returning the same
  `{severity, clip, message}` shape as `MMAnimationLibraryTools::validate_library()`.
- **`MotionMatchingDatabase.format_version`** — stamped by `finalize()`,
  checked by `is_format_compatible()` and `validate()`; defaults to `0`
  for databases saved before this field existed.
- **`MMPoseSearch::search_brute_force_query()`** — GDScript-callable
  wrapper around the existing brute-force reference search, added
  specifically to make a tree-vs-brute-force equality test possible.
- **3 new GDScript tests**: `test_kdtree_vs_bruteforce.gd`,
  `test_root_motion_continuity.gd`, `test_resource_validation.gd`
  (bringing the suite to 8 total). **None of the 8 have ever been
  executed** — see `PRE_RELEASE_REVIEW.md`.
- **`docs/CLASS_REFERENCE.md`, `docs/SYSTEMS.md`,
  `docs/ARCHITECTURE_FULL.md`, `docs/HANDOFF_PACKAGE.md`,
  `docs/PRE_RELEASE_REVIEW.md`** — this documentation set.

### Changed
- `docs/optimization.md` — corrected a false claim that cache size and
  quantization step are resource-tunable; they are fixed internal
  constants.
- `docs/api.md` — added the new opt-in subsystems, the new
  `get_debug_info()` keys, `validate()`, and `format_version`.

### Known issues (not yet fixed — see `PRE_RELEASE_REVIEW.md` for full detail)
- `demo/` has no `project.godot` — cannot currently be opened as a Godot
  project despite `demo/README.md` instructing exactly that.
- Three public boolean accessors don't follow the codebase's own `is_X`/
  `has_X` convention: `MMFeatureSchema::get_include_bone_velocity()`,
  `get_include_root_velocity()`, `MotionMatchingController::get_ground_state()`.
- `MMPlaybackMode` enum is declared, documented, and registered, but never
  used as a property or branched on anywhere.
- `MM_TAG_USER_0`/`_1`/`_2`/`MM_TAG_NONE` have no `BIND_ENUM_CONSTANT`,
  unlike the other 28 tag values.
- `-ffast-math`/`/fp:fast` in release builds, combined with pervasive
  `MM_INFINITY` sentinel comparisons — a plausible but unconfirmed
  release-only behavioral risk.
- Database build loop is single-threaded despite being structured to
  allow parallelizing across clips.
- No real compile/link/run has ever been performed for this project — all
  verification to date is static analysis and source inspection.

## Earlier history (pre-changelog, reconstructed from session records)
- Initial repo scaffold: `include/`, `src/`, `editor/`, `demo/`, `docs/`,
  `tests/`, build files, `LICENSE` (MIT), `AUTHORS`.
- Universal rig detection consolidated onto a single canonical pipeline
  (`MMSkeletonProfile` + `MMClipAnalyzer`), removing a duplicate
  `rig_profile`/`tag_rules` system.
- Full source-inspected audit of all 35 subsystems (see historical
  `AUDIT_REPORT.md`), followed by a Priority-1 fix pass (see historical
  `BUILD_READINESS_REPORT.md`).
