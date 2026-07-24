# MOTION MATCHING — Internal Alpha Release Audit (v0.1.0-alpha)

Prepared as an engineering release audit for an internal testing build,
not a public release. Evidence categories used throughout, exactly as
requested:

- **VERIFIED BY SOURCE** — confirmed by directly reading the
  implementation; the code demonstrably does what's claimed.
- **VERIFIED BY RUNTIME TEST** — confirmed by an actual execution on a
  real machine.
- **NOT YET VERIFIED** — plausible, expected, or designed-for, but
  neither of the above has happened.

**The single most important fact in this entire audit:** as of this
writing, **no line of this addon's C++ has ever been compiled, linked, or
executed**, and **none of the 8 GDScript tests in `tests/gdscript/` have
ever been run**, in any development session that produced this or any
prior document. Every "VERIFIED BY RUNTIME TEST" label below will
therefore read as absent — because it is. This is disclosed once here in
full and not repeated as a caveat on every line, but it should be read as
implicitly attached to every claim that isn't explicitly source-only.

---

## 1. Alpha Release Readiness

**What "ready for alpha" means here:** not "confirmed working" — nothing
is confirmed working, since nothing has run. It means: statically
verified to be internally consistent (compiles-clean by mechanical
analysis, no known dangling symbols, no known dead-integration bugs, no
known thread-safety defect), and therefore a reasonable, honest thing to
hand to a tester specifically *to find out* whether it works at runtime.
That is what an alpha is for.

### Safe to expose to testers now
- **Core search loop** (`MotionMatchingController`, `MMPoseSearch`,
  KD-Tree, `MMCostFunction`) — the original, most mature part of the
  codebase; extensively source-reviewed across multiple audit passes with
  no unresolved compile-blocking defects found.
- **`MMSkeletonProfile` / `MMClipAnalyzer` / `MMFeatureExtractor`** — the
  authoring pipeline; this is exactly what an alpha tester needs to
  exercise first, since it's the entry point to everything else.
- **Editor tools** — safe to expose in the sense that they're
  `TOOLS_ENABLED`-gated and won't affect exported games even if buggy;
  their exact per-control behavior has not been re-verified line-by-line
  in the most recent documentation pass.

### Needs validation before testers rely on it
- **`MMSearchCache`, `MMProfiler`, `MMTraversal`, `MMMotionWarp`
  integration** — all four were wired into the controller in a recent
  session; all four are logically sound by source inspection but have
  **never been executed once**. These are the highest-value things for
  alpha testers to specifically exercise.
- **`MMSearchWorker` (async search)** — the threading was reviewed
  carefully and one real race was found and fixed (`set_resource()`
  reassigning a `Ref` the worker still held a raw pointer into); the fix
  itself has never been executed under real concurrent load.
- **`MMRootMotion`'s frame-jump behavior** — a dead-code defect was found
  and fixed this project (`notify_frame_jump()`'s intended
  immediate-velocity-adoption was silently not happening); the fix has
  never been observed in motion.

### Should be marked experimental
- **`MMTraversal`** and **`MMMotionWarp`** — both newly integrated, both
  explicitly opt-in, both carry known unresolved design questions (no
  automatic handling of a clip switch mid-warp; traversal's
  filter-priority placement relative to jump requests was a judgment call,
  not a certainty).
- **`MMPlaybackMode`** — do not document this as a working feature to
  testers; it is declared, registered, and documented in comments, but
  **never actually used anywhere in the codebase** (no property, no
  branch). Either hide it from alpha-facing docs or explicitly label it
  "reserved, not yet functional."

---

## 2. Testing Checklist

### Installation
- [ ] GDExtension loads without errors in the Godot editor console.
- [ ] All 28 classes appear in the Create Node / New Resource dialogs
      (24 scene-level + 4 editor-level).
- [ ] Editor tools panel (`MotionMatchingEditorPlugin`'s bottom panel)
      appears and each of its 4 sub-tools is reachable.
- [ ] **Known gap:** confirm your test project actually has a
      `project.godot` before attempting any of the above — this is a
      basic project-setup prerequisite, not an addon defect, but worth
      listing explicitly since it has tripped up verification in this
      addon's own history.

### Authoring pipeline
- [ ] Skeleton detection (`MMSkeletonProfile.auto_detect()`) succeeds on
      at least 3 distinct rig conventions (e.g., Mixamo, a generic mocap
      rig with anonymous bone names, and one other convention available
      to your team) — this is the addon's core differentiator and
      deserves the most testing attention.
- [ ] Animation import via Godot's standard importer produces usable
      `Animation` resources in an `AnimationLibrary` (standard Godot
      behavior, not addon-specific — confirm no addon-side interference).
- [ ] Clip analysis (`MMClipAnalyzer.classify()`) produces sensible
      tags/category on a known-content library (verify by eye that an
      idle clip is tagged idle, a run clip is tagged run, etc.).
- [ ] `calibrate_speed_bands()` changes classification behavior visibly
      on a library with non-default speed distribution.
- [ ] Feature extraction (`MMFeatureExtractor.build_database()`)
      completes without error on libraries of at least 3 different sizes
      (see Performance Testing below for exact size tiers).
- [ ] Database generation completes and `MotionMatchingDatabase.validate()`
      (via `MotionMatchingResource.validate()`) reports zero errors on a
      correctly-configured setup.
- [ ] KD-tree build (`MMPoseSearch.build()`) completes without error and
      `is_built()` returns true.
- [ ] `MMAnimationLibraryTools.validate_library()` correctly flags a
      deliberately broken library (missing root track, zero-length clip,
      missing tracked bone) in a negative test.

### Runtime — locomotion states
For each of the following, confirm: the correct clip category is
selected, the transition into and out of the state has no visible pop,
and `get_debug_info()`'s cost value is reasonable (not near-infinite,
which would indicate no good match was found):
- [ ] Idle
- [ ] Walk
- [ ] Run
- [ ] Sprint
- [ ] Strafe (left and right)
- [ ] Backward movement
- [ ] Turning in place
- [ ] Stopping from each speed tier
- [ ] Acceleration (idle→walk→run→sprint)
- [ ] Deceleration (reverse of the above)
- [ ] Animation switching under rapid, repeated direction changes (stress
      case for the hysteresis/cooldown logic)
- [ ] Root motion produces no positional pop across a switch (this is
      exactly what the fixed `notify_frame_jump()` behavior is supposed
      to guarantee — dedicate specific attention here)
- [ ] Blend duration and smoothness match `blend_time`/`minimum_blend_time`
      configuration

### Advanced systems
- [ ] **Foot IK** — feet plant correctly on flat ground, adapt to a slope
      and a set of stairs, lock correctly during a planted-foot phase of
      a clip, release correctly on takeoff.
- [ ] **Aim IK** — chain rotates smoothly toward a moving target, respects
      `max_angle` (does not over-rotate past the cone limit), runs
      visibly after `MMWarpModifier`'s corrections (not before).
- [ ] **Warp Modifier** — orientation warp produces a visually correct
      diagonal run without foot sliding; stride warp visibly changes step
      length with speed; lean responds to acceleration.
- [ ] **Motion Warp** — `begin_warp()`/`end_warp()` produce a visible,
      smooth curve toward the target rather than a snap; test specifically
      what happens if a clip switch occurs mid-warp (known unresolved
      edge case — document actual observed behavior, don't assume).
- [ ] **Traversal** — obstacles of each supported type are correctly
      classified; `traversal_requested` signal fires with correct
      type/target; test against geometry the raycast should and should
      not detect (false-positive/false-negative check).
- [ ] **Async search** — enable `async_search`, confirm no crash, confirm
      `get_debug_info()` values update with the expected ~1-tick latency;
      specifically stress-test calling `set_resource()` repeatedly while
      async search is active (this exercises the exact race that was
      found and fixed this project).
- [ ] **Search cache** — confirm `cache_hits` increases when the character
      holds still; confirm a category lock or traversal detection
      produces a cache miss even with an otherwise-repeated query (this
      is the specific correctness property the cache's filter-mixing was
      designed to guarantee).

---

## 3. Bug Reporting Template

```
## Bug Report

**Version:** v0.1.0-alpha (commit/build hash: ___)
**Godot Version:** (e.g., 4.3.stable)
**Platform:** (Windows / Linux / macOS / Android, + OS version)
**Hardware:** (CPU, RAM, GPU if graphics-adjacent; mobile device model if Android)

### Steps to Reproduce
1.
2.
3.

### Expected Behavior


### Actual Behavior


### Logs
(Paste the editor/console output around the time of the issue. If it's a
crash, include the full stack trace if available.)

### Screenshots / Video
(Attach if the bug is visual — animation pops, foot sliding, IK glitches,
etc. are much faster to diagnose from video than from a text description.)

### Severity
- [ ] Blocker (crash, data loss, cannot proceed)
- [ ] Critical (major feature broken, no workaround)
- [ ] Major (feature broken, workaround exists)
- [ ] Minor (cosmetic, edge case)

### Which system is affected?
(Pick from the systems list in this audit — helps route the report to
the right area.)

### Additional Context
(Database size, feature dimension count, whether async search / cache /
traversal / motion warp were enabled — these are exactly the systems this
audit flags as least-tested, so noting whether they were active is
high-value triage information.)
```

---

## 4. Performance Testing

**Important framing:** no benchmark numbers exist yet for this addon —
every number below is a *target measurement to take*, not a result being
reported. Do not publish any specific FPS/ms/MB figure as fact until it
has actually been measured on real hardware.

### Metrics to record for every test case
- Animation count (clips in the library)
- Database size (frame count × dimension, plus on-disk `.tres` size)
- Feature dimension count
- Average and worst-case search time (`get_debug_info()["search_time_usec"]`)
- Cache hit rate (`cache_hits` / (`cache_hits` + `cache_misses`))
- Frame time / FPS with the addon active vs. a baseline scene without it
- RAM usage (database in memory, KD-tree size)
- CPU usage (main thread vs. background search thread, if `async_search`
  is enabled)

### Test tiers
| Tier | Animation count | Suggested use |
|---|---|---|
| Small | ~10-30 clips | Smoke test, CI-friendly, fast iteration |
| Medium | ~100-200 clips | Representative of a typical shipped character's moveset |
| Large / AAA-scale | 500-1000+ clips | Stress test — this is the scale the addon's own design comments (e.g., `MMDatabaseEditor`'s "800 clips" reference) explicitly target |

Record all metrics above at each tier, plus:
- KD-tree build time at each tier (one-time cost, but still worth
  knowing for editor-workflow iteration speed).
- Search time delta between the KD-tree path and
  `search_brute_force_query()` at each tier — this is the number that
  actually justifies the tree's existence, and it has never been
  measured.
- Whether `-ffast-math`/`/fp:fast` (enabled in `template_release` builds)
  changes search results compared to `template_debug` — a specific,
  previously-flagged, unconfirmed risk this audit inherits from prior
  review.

---

## 5. Release Notes — v0.1.0-alpha

### New Features
- Full motion-matching search pipeline: trajectory prediction, KD-tree
  nearest-neighbor search, cost-function-driven matching, hysteresis-gated
  clip switching, root motion integration.
- Universal rig detection (`MMSkeletonProfile`) — structural + name-token
  + manual-override detection, no hardcoded bone names or animation-pack
  assumptions.
- Motion-measured clip classification (`MMClipAnalyzer`) — tags/category
  derived from actual movement, not file names.
- Search result cache, background async search worker, and a search-time
  profiler, all integrated into the controller this alpha cycle.
- Optional traversal detection and motion warping, both opt-in.
- Foot IK, aim IK, and pose-level warp modifiers as separate skeleton
  modifiers.
- Full editor tooling: database builder/validator, feature/cost editor,
  trajectory tuner, live debug readout.
- Structured resource validation (`MotionMatchingResource.validate()`)
  and database format versioning.

### Known Limitations
- **No successful build or test execution has occurred for this codebase
  as of this release.** This alpha's primary purpose is to obtain that
  first real-world verification.
- `search_completed` signal is declared but never emitted — do not build
  logic depending on it.
- `MMPlaybackMode` is declared and registered but has no actual effect
  anywhere.
- Three public boolean accessors don't follow the addon's own naming
  convention (`get_include_bone_velocity`, `get_include_root_velocity`,
  `get_ground_state`) — expect these to be renamed to `is_*` before a 1.0
  release; avoid depending on the current names in long-lived test
  scripts if possible.
- `MM_TAG_USER_0`/`_1`/`_2`/`MM_TAG_NONE` are not exposed as scriptable
  constants yet.
- Database build (feature extraction) is single-threaded.
- Cache size and quantization step are fixed constants, not
  project-tunable.
- No automatic handling of a clip switch occurring mid-motion-warp.

### Experimental Systems (subject to change without notice)
`MMTraversal`, `MMMotionWarp`, and their controller-level integration
points (`begin_warp`/`end_warp`/`set_traversal`/`traversal_requested`).

### Current Verification Status
Every system in this release has been **VERIFIED BY SOURCE** (reviewed
for internal consistency, dead code, thread-safety, and known compile
hazards) and **NOT YET VERIFIED BY RUNTIME TEST** (nothing has been
compiled or executed). This is precisely the gap this alpha exists to
close.

---

## 6. Final Recommendation — Per-System Classification

| System | Classification | Evidence |
|---|---|---|
| `MotionMatchingController` | **READY FOR ALPHA** | VERIFIED BY SOURCE — mature, multi-pass reviewed core orchestrator; one real defect found and fixed this project (`set_resource()` race) |
| `MMPoseSearch` (incl. KD-Tree) | **READY FOR ALPHA** | VERIFIED BY SOURCE — algorithm and structure reviewed; correctness test authored (`test_kdtree_vs_bruteforce.gd`) but NOT YET VERIFIED BY RUNTIME TEST |
| `MMCostFunction` | **READY FOR ALPHA** | VERIFIED BY SOURCE; `switch_penalty`'s actual effect on switching decisions flagged as unconfirmed in an earlier review, carried forward here as NOT YET VERIFIED |
| `Search Cache` (`MMSearchCache`) | **NEEDS TESTING** | VERIFIED BY SOURCE for the wiring and the filter-mixing correctness design; zero runtime confirmation that hit rates behave as intended |
| `Async Search Worker` (`MMSearchWorker`) | **NEEDS TESTING** | VERIFIED BY SOURCE, including a real race found and fixed; the fix itself is NOT YET VERIFIED under real concurrent load — recommend this be a specific, named alpha test priority |
| `MMRootMotion` | **NEEDS TESTING** | VERIFIED BY SOURCE, including a real dead-code defect found and fixed (frame-jump velocity adoption); NOT YET VERIFIED that the fix produces the intended visual result |
| `MMMotionWarp` | **NEEDS TESTING / EXPERIMENTAL** | VERIFIED BY SOURCE for the mechanism; mid-warp clip-switch behavior is an explicitly unresolved design question, NOT YET VERIFIED in any form |
| `MMTraversal` | **NEEDS TESTING / EXPERIMENTAL** | VERIFIED BY SOURCE for the wiring; NOT YET VERIFIED against any real physics geometry |
| `MMFeatureExtractor` | **READY FOR ALPHA** | VERIFIED BY SOURCE — core, well-established build pipeline; single-threaded build loop is a known, non-blocking limitation |
| `MMFeatureSchema` | **READY FOR ALPHA** | VERIFIED BY SOURCE — stable layout/offset logic |
| `MotionMatchingDatabase` | **READY FOR ALPHA** | VERIFIED BY SOURCE, including this cycle's `format_version` addition; native `Resource` serialization is standard Godot behavior (EXPECTED, not independently re-executed) |
| `MMSkeletonProfile` | **READY FOR ALPHA** | VERIFIED BY SOURCE — the addon's core differentiator; recommend this receive the *most* alpha tester attention across the widest variety of real rigs, since its generality is the entire value proposition and is NOT YET VERIFIED against real-world rig diversity |
| `MMClipAnalyzer` | **READY FOR ALPHA** | VERIFIED BY SOURCE; default thresholds are reasonable but calibration (`calibrate_speed_bands()`) is recommended, not guaranteed-unnecessary, for unusual content |
| `MMFootIKModifier` | **NEEDS TESTING** | VERIFIED BY SOURCE, including this project's diagnostic improvements for misconfigured bone names; NOT YET VERIFIED against real slopes/stairs/locking behavior |
| `MMAimIKModifier` | **NEEDS TESTING** | VERIFIED BY SOURCE; ordering dependency on `MMWarpModifier` is documented but NOT YET VERIFIED in a live scene |
| `MMWarpModifier` | **NEEDS TESTING** | VERIFIED BY SOURCE; NOT YET VERIFIED visually |
| `AnimationNodeMotionMatching` | **READY FOR ALPHA** | VERIFIED BY SOURCE — the absolute-time blend mechanism is well-understood and central to the whole technique |
| `MMDatabaseEditor` | **READY FOR ALPHA** | VERIFIED BY SOURCE at the "purpose and role" level; exact per-control behavior NOT independently re-verified this cycle |
| `MMFeatureEditor` | **READY FOR ALPHA** | Same basis as above |
| `MMTrajectoryEditor` | **READY FOR ALPHA** | Same basis as above |
| `MMDebugTools` | **NEEDS TESTING** | VERIFIED BY SOURCE for its data source (`get_debug_info()`); NOT confirmed whether the panel's own display code has been updated for this cycle's newest debug keys (profiler report, traversal/warp state) |
| `MotionMatchingEditorPlugin` | **READY FOR ALPHA** | VERIFIED BY SOURCE — registration/lifecycle symmetric and correct |

### Items that are BLOCKER BEFORE PUBLIC RELEASE (not blocking for alpha, but must resolve before 1.0)
- **No successful build has ever been achieved.** This is the actual
  release blocker underlying every other item in this table — alpha
  testers achieving a real build is this release's primary purpose.
- `search_completed` signal being permanently dead — fix or remove before
  public release, since a public API surface with a documented-sounding
  but non-functional signal is a support burden.
- The 3 public naming inconsistencies (Section on Release Notes above) —
  free to fix now, breaking afterward.
- `MMPlaybackMode` — resolve (wire or remove) before public release.
- The `-ffast-math` release-build risk — must be confirmed or refuted
  with real before/after search-result data before a public release ships
  release builds with confidence.

### Summary
No system in this addon carries a **VERIFIED BY RUNTIME TEST** label,
because none has ever run. The classification above is therefore a
*source-confidence* ranking, not a *proven-working* ranking, and this
distinction should be stated explicitly to alpha testers rather than
implied away. The addon's design and static consistency are, on the
evidence gathered across this project's audits, in good shape; whether
that translates into correct runtime behavior is precisely the open
question this alpha release exists to answer.
