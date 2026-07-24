# MOTION MATCHING — Build Preparation & Runtime Validation Handoff Package

Prepared entirely by static analysis and source inspection. **No code in this
project has ever been successfully compiled or executed in any session that
produced this document.** Every claim below is labeled:

- **[STATIC]** — VERIFIED BY STATIC ANALYSIS (a script was actually run; output is reproducible)
- **[SOURCE]** — VERIFIED BY SOURCE INSPECTION (a human/AI read the exact lines and the claim follows directly)
- **[EXPECTED]** — EXPECTED BEHAVIOR ONLY (standard, well-established behavior relied on without local confirmation)
- **[INFERENCE]** — INFERENCE / EDUCATED JUDGMENT (risk assessment, priority, or design opinion)
- **[EXECUTED]** — VERIFIED BY EXECUTION (actually run on a machine and observed)

Only one fact in this entire package carries **[EXECUTED]**, and it is about
the sandbox, not the addon: a backgrounded `scons` process does not survive
a tool-call boundary in the authoring environment, and one such attempt left
`godot-cpp/gen/` empty (938 generated files → 0). This is disclosed in full
in Section 1.

---

## 1. Final Pre-Build Review

### 1.1 SConstruct **[SOURCE]**
Read in full. C++20 flag applied per-platform (`/std:c++20` for MSVC,
`-std=c++20` otherwise) **[SOURCE]**. Include paths added: `include/`,
`src/`, `editor/` **[SOURCE]**. Editor sources (`editor/*.cpp`) conditionally
globbed in only for `target in (editor, template_debug)` **[SOURCE]**.
Release builds add `-O3 -ffast-math` (GCC/Clang) or `/O2 /fp:fast` (MSVC)
**[SOURCE]** — see Section 4 for the associated risk. Output library naming
follows `libmotionmatching{suffix}{SHLIBSUFFIX}` for
Windows/Linux/Android, a `.framework` bundle for macOS, and a `.a` for iOS
**[SOURCE]**. Whether `env["suffix"]`, as actually produced by godot-cpp's
own `SConstruct` at build time, generates exactly the filenames the
`.gdextension` file expects is **[EXPECTED]** — this depends on godot-cpp's
own SConstruct internals, which I did not execute.

### 1.2 CMakeLists.txt **[SOURCE]**
Read in full. Mirrors SConstruct's include paths, `TOOLS_ENABLED` gating,
and the same `-ffast-math`/`/fp:fast` release flags **[SOURCE]**. Output
directory set to `demo/addons/motion_matching/bin` via
`LIBRARY_OUTPUT_DIRECTORY`/`RUNTIME_OUTPUT_DIRECTORY` — matches SConstruct's
target directory **[SOURCE]**. `MM_BUILD_TESTS` option is deliberately inert
with an explanatory `message(STATUS ...)` pointing at the real GDScript test
runner, since GDCLASS types need a live Godot engine and can't be linked
into a standalone test executable **[SOURCE]**.

### 1.3 register_types.cpp **[SOURCE]**
Read in full. 28 `GDREGISTER_CLASS` calls, split across
`MODULE_INITIALIZATION_LEVEL_SCENE` (24 runtime classes) and
`MODULE_INITIALIZATION_LEVEL_EDITOR` (4 editor classes, `TOOLS_ENABLED`
gated) **[SOURCE]**. `EditorPlugins::add_by_type<MotionMatchingEditorPlugin>()`
in init is paired with `remove_by_type<...>()` in uninit **[SOURCE]**.
`init_object.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE)`
is set **[SOURCE]**. 28/28 GDCLASS↔GDREGISTER match, confirmed by script
**[STATIC]** (re-run multiple times across sessions after every edit batch;
last confirmed run this session).

### 1.4 .gdextension configuration **[SOURCE]**
`demo/addons/motion_matching/motion_matching.gdextension` read in full.
`entry_symbol = "motion_matching_library_init"` matches the `extern "C"`
function name in `register_types.cpp` **[SOURCE]**. `compatibility_minimum
= "4.3"` **[SOURCE]**. Declares libraries for
windows/linux/macos/android × debug/release, plus arm64 for linux and
arm64+arm32 for android **[SOURCE]**. Whether godot-cpp's current branch
actually still builds Android arm32 (32-bit ARM support has been reduced
across recent Godot/godot-cpp releases) was **not confirmed** — I could not
find explicit arch-gating text in `godot-cpp/SConstruct` via search, and I
am not treating its absence as proof either way. **[INFERENCE]**: verify
arm32 support against the actual godot-cpp branch you vendor before relying
on the android.debug.arm32 / android.release.arm32 entries; if unsupported,
remove those two lines rather than leave a build target that can't produce
its declared binary.

### 1.5 Project structure / dependency layout — **new finding, high priority**
**[SOURCE], directly executed `find`/`ls` on the actual filesystem:**
`demo/` contains `README.md`, `character.tscn`, `demo_scene.tscn`,
`player.gd`, and `addons/motion_matching/{motion_matching.gdextension, bin/}`
— **there is no `project.godot` file anywhere in `demo/`.**
`demo/README.md` itself instructs "2. Open `demo/` as a Godot project"
**[SOURCE]** — but as the folder stands, Godot will not recognize it as a
project root at all; there is nothing to open. This is a real, verified gap
in the deliverable, not a hypothetical — see Section 5 (blocker table).

### 1.6 Include paths **[STATIC]**
Zero dangling local `#include "..."` across `include/`, `src/`, `editor/`,
`demo/`, `tests/` — script re-run this session, confirmed clean. Angle-bracket
`godot_cpp/...` include resolution was verified against the vendored
`godot-cpp` tree **earlier** in this engagement, before an accidental
`scons` invocation left `godot-cpp/gen/` empty (see 1.7) — that specific
check was **[STATIC]** at the time it ran, but has not been re-confirmed
since, so treat it as **[STATIC, PRE-WIPE, NOT RE-CONFIRMED]**.

### 1.7 Disclosure: godot-cpp/gen/ is currently empty **[EXECUTED]**
In an earlier turn, a fully-detached (`nohup`, redirected, backgrounded)
`scons` build was launched to test whether it could survive past a single
tool call. A follow-up call confirmed via `ps aux` that the process was gone
and `find godot-cpp/gen -type f | wc -l` returned `0` (previously ~939).
`godot-cpp/include/` (63 files) and `godot-cpp/src/` (62 files) — the
hand-written parts — are untouched, confirmed by the same `find` commands
**[EXECUTED]**. The addon's own source (`include/`, `src/`, `editor/`,
`demo/`, `tests/`, `docs/`) is unaffected, confirmed by re-running the
dangling-include and GDCLASS scripts against those folders specifically
**[STATIC]**. **On a real machine, cloning godot-cpp fresh (or running
`git submodule update --init --recursive`) regenerates this automatically as
part of any real build** — this is **[EXPECTED]**, not something re-tested
here, per your instruction not to invoke scons again.

### 1.8 Library output paths **[SOURCE]**
Both SConstruct and CMakeLists.txt target
`demo/addons/motion_matching/bin/`, matching where the `.gdextension` file
expects binaries **[SOURCE]**. `bin/` currently contains only `.gitkeep`
**[EXECUTED — directly `ls`'d]**, i.e., no stale/leftover binaries that
could mask a build failure.

---

## 2. Build Readiness Checklist

Every command, version number, and "expected output" below is
**[EXPECTED]** unless otherwise noted — none of it has been executed in
this environment.

### Linux
- Required: `git`, `python3` + `pip`, `scons` (`pip install scons`), a
  C++20-capable compiler (GCC ≥ 10 or Clang ≥ 12) **[EXPECTED]**
- Godot version: 4.3+ (per `.gdextension`'s `compatibility_minimum`) **[SOURCE]**
- godot-cpp branch: `4.3` (per prior session's clone; should match your
  actual Godot version) **[SOURCE, from earlier session's clone command]**
- No special env vars required beyond a working compiler on `PATH` **[EXPECTED]**
- Commands:
  ```bash
  git submodule update --init --recursive
  cd godot-cpp && scons platform=linux target=template_debug -j$(nproc) && cd ..
  scons platform=linux target=template_debug -j$(nproc)
  ```
- Expected output: `demo/addons/motion_matching/bin/libmotionmatching.linux.template_debug.x86_64.so` **[EXPECTED]**

### Windows (MSVC)
- Required: Visual Studio 2022 (17.x) with the C++ workload, Python 3.x,
  SCons **[EXPECTED]**
- `/std:c++20` requires VS2022; VS2019 is expected to reject it **[EXPECTED]**
- Commands:
  ```powershell
  git submodule update --init --recursive
  cd godot-cpp; scons platform=windows target=template_debug; cd ..
  scons platform=windows target=template_debug
  ```
- Expected output: `demo\addons\motion_matching\bin\libmotionmatching.windows.template_debug.x86_64.dll` **[EXPECTED]**

### macOS
- Required: Xcode command line tools (`xcode-select --install`) **[EXPECTED]**
- Command: `scons platform=macos target=template_debug arch=universal`
- Expected output: a `.framework` bundle per SConstruct's macOS branch
  **[SOURCE]** (path pattern read directly from the file)
- Known friction: an unsigned `.framework` may be quarantined by Gatekeeper;
  `xattr -dr com.apple.quarantine` on the built bundle is the standard
  workaround **[EXPECTED]**

### Android
- Required: Android NDK (version must match what your godot-cpp branch
  supports — **not verified locally**, check `godot-cpp/README.md` on your
  actual checkout), `ANDROID_NDK_ROOT` / `ANDROID_HOME` set **[EXPECTED]**
- Command: `scons platform=android target=template_debug arch=arm64`
- Expected output: `libmotionmatching.android.template_debug.arm64.so` **[EXPECTED]**
- **Open item from 1.4:** confirm arm32 is actually still supported by your
  godot-cpp branch before relying on those `.gdextension` entries **[INFERENCE]**

---

## 3. Runtime Validation Plan

None of this has been executed. This is a plan to hand to whoever has a
real build, not a report of results.

### Initialization
1. Open `demo/` in the Godot editor **once `project.godot` is added — see
   Section 5, blocker RB-1** — confirm no GDExtension load errors in the
   output panel.
2. Confirm `MotionMatchingController`, `MotionMatchingResource`, and the
   other 26 registered classes appear in the Create Node / New Resource
   dialogs.
3. Confirm the **Motion Matching** editor bottom panel appears (proves
   `MODULE_INITIALIZATION_LEVEL_EDITOR` classes registered).

### Motion Matching core
4. Build a database from a real skeleton + animation library via the editor
   panel (Scan → Build database → Save); confirm `MotionMatchingDatabase`
   reports a non-zero frame count and `is_format_compatible() == true`.
5. Assign the database to a `MotionMatchingController`, add it under an
   `AnimationTree`, enter Play mode; confirm `get_debug_info()` reports a
   valid `frame_index`/`animation_id` within the first few frames.
6. Compare `get_debug_info()["cache_hits"]`/`["cache_misses"]` before and
   after holding the character still — hits should climb once the character
   stops moving (this is the behavior `_make_cache_key()` was designed to
   produce, per source; genuinely confirming it requires this exact test).
7. Read `get_debug_info()["profiler"]` after ~5 seconds of movement; confirm
   it contains non-zero search-time percentiles.
8. Assign an `MMTraversal` via `set_traversal()`, walk the character at an
   obstacle with real collision geometry, confirm `traversal_requested`
   fires (connect a test signal handler and log it).
9. Assign an `MMMotionWarp`, call `begin_warp(target_transform)`, confirm
   root motion visibly curves toward the target rather than continuing in
   the clip's original direction.
10. Confirm root motion doesn't visibly pop/stutter across an ordinary
    motion-matching switch (this is what `test_root_motion_continuity.gd`
    checks in isolation, but a real-scene visual check is the ultimate test).

### Stress testing
11. Force `_run_search()` to run every frame for several thousand frames
    (e.g., set `search_interval = 0`, run for a few minutes); watch for
    memory growth (a leak would show as unbounded RSS growth over time).
12. Call `set_resource()` repeatedly on a running controller with async
    search enabled, from a script that also spams movement input, to
    specifically stress the race window closed in this session's
    `set_resource()` fix — this is the single most important stress test to
    run, since it directly exercises a fix that was never executed.
13. Call `rebuild()` while a search is genuinely in flight (async mode,
    tight timing) repeatedly, watch for crashes or stale results.
14. Swap `MotionMatchingResource` at runtime (hot reload) several times in
    a row; confirm no crash and that `get_debug_info()` reflects the new
    resource's data, not stale data from the old one.

### Performance
15. Measure average `get_debug_info()["search_time_usec"]` over a few
    thousand frames, in both `template_debug` and `template_release` builds
    — specifically watch for any behavioral difference between the two
    given the `-ffast-math`/`MM_INFINITY` concern in Section 4.
16. Compare `search_query()` vs `search_brute_force_query()` timing (both
    now GDScript-callable) on a database of realistic size (hundreds to
    low thousands of frames) to get a real speedup number — this was never
    measured, only reasoned about.
17. Track `get_debug_info()["update_total_usec"]` against your target frame
    budget on your actual target hardware (this project's mobile-FPS/TPS
    context makes this specifically worth checking on real Android
    hardware, not just desktop).

### Regression
18. Run `godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd`
    (once a build + `project.godot` exist) and confirm all 8 scripts pass —
    5 pre-existing + 3 added this session (`test_kdtree_vs_bruteforce.gd`,
    `test_root_motion_continuity.gd`, `test_resource_validation.gd`). **No
    evidence currently exists that any of the 8 pass** — this is the single
    highest-value first step once a real build is available.

---

## 4. Bug Hunt — Verified Findings Only

Scope: UB, thread safety, lifetime, `Ref<>` ownership, races, deadlocks,
leaks, dangling pointers, invalid references, integer overflow, float edge
cases. Only findings that could actually be traced through the source are
listed; nothing here is speculative.

**4.1 — `MotionMatchingController::set_resource()` — UAF race — FIXED THIS SESSION [SOURCE]**
Traced exact call sequence: `set_resource()` called `_sync_from_resource()`
(which reassigns `_cost_function`, `_database`, `_schema` — dropping old
`Ref`s, potentially destroying the objects if refcount hits zero) before
`rebuild()`'s `_worker.stop()` ran. If async search was enabled and a search
was in flight at that moment, the worker thread's raw `_cost` pointer
(handed to it by a prior `_worker.start()`) could go dangling mid-search.
Fix applied: `_worker.stop()` moved to the top of `set_resource()`. The
existence of the ordering bug, and that the fix closes it, are both
**[SOURCE]** (the ordering is directly readable in the diff); that this
would *actually* manifest as a crash under real thread timing is
**[EXPECTED]** — never executed, never observed happening.

**4.2 — `MMRootMotion::notify_frame_jump()` — dead state — FIXED THIS SESSION [SOURCE]**
`_blend_linear`/`_blend_angular` were written by `notify_frame_jump()` but
never read anywhere in `update()`, confirmed by exhaustive grep across both
files showing zero read sites before the fix. Not a memory-safety bug (no
UB, no crash risk) — a correctness/dead-code bug: the function's own
comment described immediate-velocity-adoption behavior that did not
actually occur. Fixed by adding a `_jump_pending` flag consumed on the next
tick.

**4.3 — `MMSearchWorker::_thread_main()` — Ref-promotion pattern — reviewed, no defect found [SOURCE]**
`Ref<MMCostFunction> cost = Ref<MMCostFunction>(const_cast<MMCostFunction*>(_cost));`
temporarily promotes a raw pointer to an owning `Ref` for the duration of
one search call, which — given Godot's `RefCounted` uses an atomic
refcount — is a legitimate, correct pattern to prevent the main thread from
destroying the object mid-search **[SOURCE for what the code does;
EXPECTED for why it's safe, since this relies on documented `RefCounted`
atomicity, not a demonstrated concurrent run]**. `_search` (the KD-tree) is
not given the same treatment, but is protected differently: `rebuild()`
calls `_worker.stop()` (a full thread join) before rebuilding the tree, so
access is serialized by thread lifecycle rather than refcounting — this
asymmetry is intentional, not a gap **[SOURCE]**, traced by reading
`rebuild()`'s exact statement order.

**4.4 — `set_resource()`, second call site check — no additional defect found [SOURCE]**
Confirmed both call sites of `_sync_from_resource()` (`_ready()` and
`set_resource()`) are followed synchronously by `rebuild()` with no
yielding in between, so no *other* code path bypasses the fix in 4.1.

**4.5 — Integer overflow — not found, narrowly scoped check [SOURCE]**
Checked `MMSearchCache`'s key-mixing arithmetic (`_make_cache_key()`,
added this session) — uses `uint64_t` throughout with well-defined wraparound
semantics (unsigned overflow is defined behavior in C++, not UB); no signed
overflow present in that specific function. This is **not** an exhaustive
integer-overflow audit of all ~35 files — it covers only the code added or
touched this session, stated honestly rather than implying full coverage.

**4.6 — Deadlock check on `MMSearchWorker` — no defect found, narrow scope [SOURCE]**
`stop()`'s sequence (`_running.store(false)` → `_signal.notify_all()` →
`_thread.join()`) and `_thread_main()`'s wait predicate
(`_has_request.load() || !_running.load()`) were read together: the
predicate correctly wakes the thread on either condition, so `stop()`
cannot block forever waiting on a thread stuck in `wait()`. This check is
scoped to `thread_pool.cpp` only.

**4.7 — Floating point edge cases — flagged as risk, not confirmed [INFERENCE]**
`-ffast-math`/`/fp:fast` in release builds, combined with `MM_INFINITY`
used as a sentinel throughout `pose_search.cpp`/`cost_function.cpp`
(`best_cost_seed`, cost comparisons). Comparisons like `cost < MM_INFINITY`
are generally safe even under fast-math in common compilers, but this is
exactly the class of bug that would only appear in `template_release` and
never in `template_debug`. **Not confirmed either way** — flagged as a
targeted release-build test item, not a certain defect.

**No other verified UB, leak, dangling-pointer, or race findings** beyond
4.1–4.7 — this reflects the depth of review actually performed this
session, not a claim that the remaining ~30 files are proven clean.

---

## 5. Release Blocker Report

| ID | Severity | File | Function | Root cause | Evidence | Impact | Fix |
|---|---|---|---|---|---|---|---|
| RB-1 | **Critical** | `demo/` (missing file) | n/a | No `project.godot` exists anywhere in `demo/` | **[EXECUTED — directly `find`'d]** | `demo/README.md`'s own step 2 ("Open `demo/` as a Godot project") cannot be performed; the demo is not currently an openable Godot project at all | Add a minimal `project.godot` (name, main scene pointing at `demo_scene.tscn`, input map entries for `move_left/right/forward/back/sprint/jump` per `demo/README.md`'s documented requirements) |
| RB-2 | **High** | `src/motion_matching.cpp` | `set_resource()` | UAF race between resource-Ref reassignment and in-flight async search | **[SOURCE]**, fixed this session | Rare, timing-dependent crash/corruption if `set_resource()` is called while async search is in flight | **Already fixed** — verify with stress test #12 in Section 3 once buildable |
| RB-3 | **Medium** | `include/mm_types.hpp` | `MMPlaybackMode` enum | Declared, `VARIANT_ENUM_CAST`'d, documented intent, never used as a property or branched on anywhere | **[SOURCE]** | No functional bug, but a documented-sounding feature that silently does nothing if a user expects it to switch behavior | Either wire it into `AnimationNodeMotionMatching`/`MotionMatchingController`, or remove it and its doc comments to avoid implying unbuilt functionality |
| RB-4 | **Medium** | `include/mm_types.hpp` / binding site | `MMTag` enum bindings | `MM_TAG_USER_0/1/2` and `MM_TAG_NONE` have no `BIND_ENUM_CONSTANT`, unlike the other 28 tag values | **[SOURCE]** | GDScript cannot reference `MMTag.MM_TAG_USER_0` etc., defeating the documented "user-extensible" purpose of those slots | Add the missing `BIND_ENUM_CONSTANT` calls (4 lines) |
| RB-5 | **Medium** | `SConstruct`, `CMakeLists.txt` | release config | `-ffast-math` / `/fp:fast` combined with pervasive `MM_INFINITY` sentinel usage | **[INFERENCE]** | Possible release-only behavioral divergence in search cost comparisons; not confirmed | Specifically diff `template_debug` vs `template_release` search results once buildable (Section 3, item 15) before shipping release builds |
| RB-6 | **Low** | `.gdextension` | android arch entries | arm32 support not locally confirmed against the vendored godot-cpp branch | **[INFERENCE]** | If unsupported, the declared `android.debug.arm32`/`android.release.arm32` entries point at a binary that can never be produced | Confirm against your actual godot-cpp branch's docs; remove the two lines if unsupported |
| RB-7 | **Low** | `godot-cpp/gen/` | vendored dependency | Currently empty (0 files) due to an interrupted build attempt in this sandbox | **[EXECUTED]** | None on a real machine — any real build regenerates this directory as a normal first step | No action needed beyond a normal `git submodule update` / build on the target machine |
| RB-8 | **Low** | Whole repo | test execution | All 8 GDScript tests (5 pre-existing + 3 added) have never been run once | **[SOURCE — zero execution evidence exists]** | Unknown pass/fail state for the entire test suite | Run `run_tests.gd` as the very first step on a real machine, before anything else in Section 3 |

---

## 6. Final Handoff — Project Reference

### Architecture
Five layers, **[SOURCE]** per `docs/architecture.md` and direct code
reading: **Data** (`MMFeatureSchema`, `MMAnimationEntry`,
`MotionMatchingDatabase`) → **Build** (`MMSkeletonProfile`,
`MMClipAnalyzer`, `MMFeatureExtractor`, `MMAnimationLibraryTools`) →
**Search** (`MMCostFunction`, `MMPoseSearch` — KD-tree, `MMSearchCache`,
`MMSearchWorker`, `MMProfiler`) → **Motion**
(`MMTrajectory`, `MMRootMotion`, `MMTraversal`, `MMMotionWarp`) →
**Runtime** (`MotionMatchingResource`, `MotionMatchingController`,
`AnimationNodeMotionMatching`, `MMDebugDraw`), plus skeleton modifiers
(`MMWarpModifier`, `MMIKSolver`, `MMFootIKModifier`, `MMAimIKModifier`) and
editor tooling (`MMDatabaseEditor`, `MMFeatureEditor`, `MMTrajectoryEditor`,
`MMDebugTools`, `MotionMatchingEditorPlugin`).

### Folder structure
```
include/    35 headers, one class family per file
src/        matching .cpp implementations (some split, see 1.3-style
            pairings: cache.hpp -> cache_manager.cpp + thread_pool.cpp;
            motion_matching.hpp -> motion_matching.cpp + motion_matching_database.cpp)
editor/     TOOLS_ENABLED-gated editor plugin + panels
demo/       minimal project wiring (currently missing project.godot — RB-1)
tests/      GDScript test suite (8 scripts), README explaining why not C++
docs/       architecture.md, workflow.md, api.md, optimization.md, this file
godot-cpp/  vendored dependency (submodule) — gen/ currently empty, see 1.7
```

### Major systems
Universal rig detection (structural graph analysis → normalized name
matching → manual override, zero hardcoded bone names) and motion-driven
clip classification (measures speed/turn-rate/airtime rather than reading
clip names) are the two design pillars that make this addon
animation-pack-agnostic — both **[SOURCE]**, read directly out of
`skeleton_profile.hpp`/`clip_analysis.hpp` and confirmed in prior sessions'
audits.

### Dependencies
`godot-cpp` (submodule, branch should match target Godot version — this
project's prior sessions used `4.3`), C++20 compiler, SCons or CMake.

### Build sequence
See Section 2. In order: clone with submodules → build godot-cpp → build
this addon → confirm output binary lands in
`demo/addons/motion_matching/bin/` → (once RB-1 is fixed) open `demo/` in
the editor.

### Runtime sequence
`register_types.cpp`'s `initialize_motion_matching_module` runs at
`MODULE_INITIALIZATION_LEVEL_SCENE` (registers 24 classes) then, if
`TOOLS_ENABLED`, at `MODULE_INITIALIZATION_LEVEL_EDITOR` (registers 4 more
+ the editor plugin) **[SOURCE]**. At runtime, a `MotionMatchingController`
node's `_ready()` resolves its character, syncs from its
`MotionMatchingResource`, and calls `rebuild()` (which builds the KD-tree,
clears the cache, and starts the async worker if enabled) **[SOURCE]**.
Every `_physics_process()` tick: trajectory update → optional traversal
probe → playback advance → async response poll (if any) → search (if due)
→ root motion integration **[SOURCE]**, per `update()`'s exact statement
order.

### Testing sequence
`godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd`
runs all 8 scripts and exits non-zero on any failure — **never actually run
in this environment**; running it is the single most valuable first action
on a real machine (RB-8).

### Known limitations (all **[SOURCE]** unless marked)
- Database build loop is single-threaded despite being structured to allow
  parallelizing across clips.
- Cache size (1024) and quantization step (0.15) are internal constants,
  not resource-tunable.
- No automatic handling if a clip switch happens mid-motion-warp; caller's
  responsibility.
- `MMPlaybackMode` is unwired (RB-3).
- `MM_TAG_USER_0/1/2`/`MM_TAG_NONE` unbound (RB-4).
- Editor debug panel doesn't yet surface the newer profiler/traversal/warp
  `get_debug_info()` keys.
- **[INFERENCE]** `-ffast-math` release-build risk, unconfirmed (RB-5).

### Future improvements (**[INFERENCE]**, prioritized opinion, not fact)
1. Fix RB-1 (add `project.godot`) — blocks everything else, trivial to fix.
2. Run the existing 8 tests for the first time on a real build.
3. Resolve RB-3/RB-4 (either wire up or remove the dead pieces).
4. Confirm or refute the RB-5 fast-math concern with an actual before/after
   comparison.
5. Parallelize the database build loop for large libraries.
6. Make cache size/quantization resource-tunable if a project needs a
   different hit-rate/precision tradeoff than the current constants.
