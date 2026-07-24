# MOTION MATCHING — Final Pre-Release Code Review

Zero-change review. No code was modified to produce this document. Every
claim is labeled:

- **[STATIC]** — VERIFIED BY STATIC ANALYSIS (a script was run; output is reproducible)
- **[SOURCE]** — VERIFIED BY SOURCE INSPECTION (exact lines read directly)
- **[EXPECTED]** — EXPECTED BEHAVIOR ONLY (standard convention, not locally re-confirmed)
- **[INFERENCE]** — INFERENCE / EDUCATED JUDGMENT (opinion, priority, or recommendation)

---

## 1. API Consistency

**Naming — 3 verified inconsistencies, all in the public (bound) API [STATIC + SOURCE]:**
A grep of every `bool`-returning method declaration across `include/*.hpp`
found 22 boolean accessors. 19 of them follow `is_X`/`has_X`
(`is_dirty`, `is_valid`, `is_built`, `is_active`, `is_warping`,
`is_format_compatible`, `is_detected`, `has_role`, etc.). Three do not:

| Method | File | Bound to GDScript? |
|---|---|---|
| `MMFeatureSchema::get_include_bone_velocity()` | `feature.hpp` | Yes — via `MM_BIND_PROPERTY`, confirmed in `src/feature.cpp:193` **[SOURCE]** |
| `MMFeatureSchema::get_include_root_velocity()` | `feature.hpp` | Yes — `src/feature.cpp:194` **[SOURCE]** |
| `MotionMatchingController::get_ground_state()` | `motion_matching.hpp` | Yes — explicit `ClassDB::bind_method` at `src/motion_matching.cpp:874` **[SOURCE]** |

All three are part of the public, scriptable surface, so renaming them
after a 1.0 release would be a breaking change for anyone's `.tscn`/`.gd`
files that reference `include_bone_velocity`, `include_root_velocity`, or
`get_ground_state()`. **[INFERENCE]:** rename to `get_includes_bone_velocity`
→ no, better: `is_bone_velocity_included`/`is_root_velocity_included` and
`is_grounded()` before 1.0, while renaming is still free.

**Const correctness [SOURCE]:** every accessor reviewed this pass
(`is_*`, `get_*`, `has_*` across the files opened this session —
`motion_matching.hpp`, `frame_database.hpp`, `root_motion.hpp`,
`pose_search.hpp`, `traversal.hpp`, `motion_warping.hpp`) is correctly
marked `const`. This is not a full 35-file re-audit; it's confirmed for the
files actually re-opened this session.

**noexcept [SOURCE, narrow scope]:** no method in this codebase is marked
`noexcept` anywhere. Trivial accessors like `is_valid()`, `get_hits()`,
`is_active()` (simple field returns, no allocation) are textbook `noexcept`
candidates **[INFERENCE]**. This is a genuine gap but a low-priority one:
Godot's own `ClassDB`/`GDExtension` binding layer does not currently take
advantage of `noexcept` for dispatch optimization as far as documented
convention goes **[EXPECTED]**, so this is a code-quality nicety, not a
functional or performance gap.

**override/final [STATIC]:** a script cross-checking every `class X :
public Y` declaration across `include/*.hpp` and `editor/*.hpp` against the
28 registered GDCLASS names found **zero cases of one of our own classes
being subclassed by another of our own classes** — every one of the 28
derives directly from a Godot engine base (`Node`, `Resource`,
`RefCounted`, etc.), never from each other. **[INFERENCE]:** every one of
these 28 classes is a safe `final` candidate in C++ terms (marking them
`final` does not prevent GDScript-side `extends` in user projects, since
that goes through `ClassDB` by name, not C++ inheritance). Currently zero
uses of `final` exist in the codebase **[STATIC — grep confirmed]**. Adding
it is a hardening/intent-signaling opportunity, not a defect.

**Property consistency [SOURCE]:** spot-checked property↔accessor pairs
across `motion_matching.hpp`/`frame_database.hpp`/`motion_matching_database.cpp`
— every `ADD_PROPERTY`/`MM_BIND_PROPERTY` call's declared name matches its
`set_*`/`get_*` pair exactly (e.g. `format_version` ↔
`set_format_version`/`get_format_version`). No mismatches found in the
files reviewed this pass.

**Signal consistency [STATIC, re-confirmed from a prior session]:** every
`emit_signal(...)` call site's argument count matches its corresponding
`ADD_SIGNAL(MethodInfo(...))` declaration — script re-run, zero mismatches.

**Documentation consistency [SOURCE, INFERENCE for the judgment]:** the
`docs/` folder (architecture.md, workflow.md, api.md, optimization.md) is
prose documentation describing the system, not inline Doxygen-style
per-method comments in headers. Inline comments in headers are present but
inconsistent in density — some classes (`cache.hpp`, `motion_matching.hpp`)
have substantial explanatory comments on non-obvious design decisions;
others (`feature.hpp`, `skeleton_profile.hpp`) have comments mainly on the
class level, less on individual methods. **[INFERENCE]:** this is
acceptable for an internal/addon codebase but would benefit from a
consistent per-method doc-comment pass (e.g., matching the style already
used in `docs/api.md`'s tables) before calling the public API "documented
to 1.0 standard."

---

## 2. Godot Integration

**GDExtension conventions [SOURCE]:** `register_types.cpp` follows the
standard current pattern — `GDExtensionBinding::InitObject`,
`register_initializer`/`register_terminator`,
`set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE)`,
`extern "C"` entry point matching the `.gdextension` file's `entry_symbol`.
Editor classes are gated behind `TOOLS_ENABLED` and the
`MODULE_INITIALIZATION_LEVEL_EDITOR` level, with `EditorPlugins::add_by_type`/
`remove_by_type` correctly paired between init and uninit.

**ClassDB bindings [STATIC]:** 28/28 GDCLASS↔GDREGISTER_CLASS match; 141
directly cross-checked `bind_method(D_METHOD(...))` sites have matching
argument-name counts against their function's parameter counts (does not
verify types, only counts — see the caveat given in the previous evidence
audit).

**Resource ownership [SOURCE]:** zero raw `new`/`delete` found anywhere in
the codebase — every `RefCounted`-derived object is created via
`.instantiate()`. The one place a raw pointer is deliberately promoted to
an owning `Ref` (`MMSearchWorker::_thread_main()`'s
`Ref<MMCostFunction> cost = Ref<MMCostFunction>(const_cast<...>(_cost))`)
was traced and found to be an intentional, correct safety measure exploiting
`RefCounted`'s atomic refcount, not an ownership violation.

**Node lifecycle [SOURCE]:** `MotionMatchingController::_notification()`
handles `NOTIFICATION_EXIT_TREE` to stop the async search worker thread —
correct cleanup timing (before the node itself is destroyed). `_ready()`
correctly early-returns and disables processing when
`Engine::get_singleton()->is_editor_hint()` is true, avoiding wasted work in
the editor.

**Editor plugin integration [SOURCE]:** `MotionMatchingEditorPlugin`
registered/unregistered symmetrically (see above). Not independently
verified beyond registration symmetry — the editor panels' actual UI
behavior was not re-reviewed this pass.

**Serialization compatibility [SOURCE]:** `MotionMatchingDatabase::format_version`
(added this session) defaults to `0` for anything saved before the field
existed, distinguishable from the current version (`1`), so old `.tres`
files won't be silently misread as compatible. `MotionMatchingResource::validate()`
surfaces a format mismatch as a structured warning rather than failing
silently. This is the correct shape for forward compatibility, but has
never been tested by actually loading a database saved by an older version
of this addon **[SOURCE for the mechanism; EXPECTED for it working correctly on a real old file]**.

---

## 3. C++ Quality

**RAII [SOURCE]:** consistent throughout — `Ref<T>` for all RefCounted
ownership, `std::thread` + `std::mutex`/`std::condition_variable` in
`MMSearchWorker` with a destructor that calls `stop()` (which joins the
thread), no manual resource management found needing a matching cleanup
that's missing.

**Move/copy correctness [SOURCE, narrow scope]:** `MMSearchFilter`,
`MMSearchContext`, `MMMatchResult`, `MMSearchStats` (all plain structs with
POD-like members — `uint32_t`, `float`, `int`) rely on compiler-generated
copy/move, which is correct for their member types. No class in the files
reviewed this session declares a custom copy constructor, move constructor,
or destructor that could violate the rule of five — meaning none needed to,
given their member types (`Ref<T>` and Godot container types all have
correct copy/move semantics of their own). This is not a full 35-file audit.

**Thread ownership [SOURCE]:** exactly one background thread in the entire
codebase (`MMSearchWorker::_thread`), lifecycle owned entirely by
`MMSearchWorker` itself (`start()`/`stop()`/destructor). No other thread
creation found anywhere. `MotionMatchingController` owns an `MMSearchWorker`
by value (`_worker`, not `Ref<>` or pointer) — its lifetime is tied
directly to the controller's, which is correct and simple.

**Lifetime management [SOURCE]:** the one verified defect this project has
found in this category — the `set_resource()` UAF race — was fixed this
session (see prior report). No other lifetime issue was found in this
review pass, though this reflects the scope of files actually re-opened
(motion_matching.*, frame_database.*, root_motion.*, pose_search.hpp,
traversal.hpp, motion_warping.hpp, thread_pool.cpp), not a full 35-file
re-audit from scratch.

**Exception safety [SOURCE]:** the codebase does not use C++ exceptions
(`throw`/`try`/`catch`) anywhere — confirmed by the absence of these
keywords in every file opened this session, consistent with Godot/godot-cpp
convention, which itself does not use exceptions for its own error
reporting (relies on `ERR_FAIL_*` macros and `Error` return codes instead).
**[EXPECTED]** this matches the wider Godot C++ ecosystem's convention,
not something unique to verify per-file.

**Undefined behavior [SOURCE, scoped to prior findings]:** the two real
bugs found this project (the `set_resource()` race, the
`notify_frame_jump()` dead state) are both fixed. No new UB was found in
this pass beyond what's already reported. The `-ffast-math`/`MM_INFINITY`
concern remains **[INFERENCE]**, not confirmed UB — it's a plausible
compiler-optimization risk, not a demonstrated one.

**Compiler portability [SOURCE]:** `SConstruct`/`CMakeLists.txt` both
branch on MSVC vs. GCC/Clang for flags (`/std:c++20` vs. `-std=c++20`,
`/fp:fast` vs. `-ffast-math`). No compiler-specific intrinsics, `#pragma`s,
or non-portable constructs were found in the source files reviewed this
session. `_FORCE_INLINE_` (used in `mm_types.hpp`, `frame_database.hpp`) is
a godot-cpp-provided macro, not a raw compiler intrinsic, so it's already
portable by construction **[EXPECTED]**.

---

## 4. Maintainability

**Duplicate code [STATIC]:** a script checking for any 4-consecutive-line
block repeated verbatim across different `src/*.cpp` files found **zero**
matches. This is a crude check (exact-match only, won't catch
near-duplicates with renamed variables), but combined with the earlier
sessions' confirmation that the rig-detection consolidation removed the
one known duplicate system, there's no further evidence of copy-paste
logic.

**Unnecessary complexity [INFERENCE]:** the codebase's complexity is
largely proportional to its stated goal (a full AAA-style motion-matching
system with universal rig support) rather than gratuitous. The one place
that reads as more complex than its current usage justifies is
`MMPlaybackMode` (Section 3 finding from the previous audit) — an enum
with real design intent but zero actual branching logic using it anywhere.

**Hidden coupling [SOURCE]:** `MotionMatchingController` is the one class
with broad knowledge of nearly every other subsystem (trajectory, root
motion, search, cache, profiler, traversal, warp) — this is expected and
appropriate for a top-level orchestrator class, not hidden coupling, since
all of it is visible in one file's includes and member list.

**Circular dependencies [STATIC]:** a script building the full `#include`
graph across every header in `include/` and running cycle detection found
**no cycles**.

**Files that should be split [INFERENCE]:** `motion_matching.hpp` /
`motion_matching.cpp` are the largest files in the project (the `.cpp` is
now ~35KB after this session's integration work) and hold both
`MotionMatchingController` and `MotionMatchingResource`. **[INFERENCE]:**
given `MotionMatchingResource`'s implementation already deliberately lives
in a *separate* file (`motion_matching_database.cpp`) while its
*declaration* stays in `motion_matching.hpp`, there's already a partial
precedent for splitting; moving `MotionMatchingResource`'s declaration into
its own header (or into `frame_database.hpp`, which it's already
conceptually adjacent to) would make `motion_matching.hpp` more clearly
"the controller's header" and reduce the file the controller's own
maintainers have to scroll through. Not urgent, not a defect — a
readability opinion.

**Files that should be merged [INFERENCE]:** none identified. The
existing split (e.g., `cache.hpp` → `cache_manager.cpp` + `thread_pool.cpp`)
is deliberate and sensible (two genuinely distinct concerns —
quantized-lookup cache vs. thread lifecycle — sharing one header because
they're both small and tightly related).

---

## 5. Public Release Readiness

**Could another developer understand this project without prior context?**
**[INFERENCE]**, but backed by concrete evidence: `docs/architecture.md`
lays out the 5-layer design, `docs/api.md` documents the public surface in
table form, `docs/workflow.md` walks through actual usage, and
`docs/HANDOFF_PACKAGE.md` (produced last session) gives a from-scratch
project map. Combined with consistent naming (modulo the 3 exceptions in
Section 1) and zero circular dependencies, a new developer has a real path
in. The gap: no per-method Doxygen-style comments (Section 1), and — as of
last session — `demo/` has no `project.godot`, so the one thing a new
developer would try first (open the demo project) currently doesn't work.
That is a **verified**, not inferred, onboarding blocker (RB-1 from the
previous report).

**Is the API stable enough for 1.0?** **[INFERENCE]:** Not quite, for two
concrete, fixable reasons: (1) the 3 naming inconsistencies in Section 1
are exactly the kind of thing that's free to fix now and a breaking change
later; (2) `MMPlaybackMode` being fully unwired means either it needs real
behavior before 1.0, or it should be removed/marked experimental so users
don't build against a no-op enum. Everything else reviewed is consistent
enough to commit to.

**Which APIs should be marked experimental?** **[INFERENCE]:**
- `MMTraversal`/`MMMotionWarp` integration on the controller — genuinely
  new this session, unexecuted, and the mid-warp clip-switch edge case is
  an explicitly open design question (Section 4 of the prior report).
- `MMPlaybackMode` — until it's actually wired to something.
- The cache's fixed quantization/size constants — likely to change once a
  real project needs different tuning (Section 5, prior report).

**Which APIs should be private (not exposed to GDScript)?**
**[INFERENCE]:** `MotionMatchingController::_make_cache_key()`,
`_evaluate_traversal()`, `_evaluate_continuation()`, and the other
underscore-prefixed helpers are already correctly kept out of
`_bind_methods()` **[SOURCE — confirmed none of these appear in any
`ClassDB::bind_method` call]**. No currently-public method looks like it
should be made private; the naming-convention fixes in Section 1 are a
rename concern, not a visibility concern.

**Which APIs should never change after release?**
**[INFERENCE]:** `MotionMatchingResource`'s property set
(`animation_library`, `database`, `schema`, `cost_function`) and
`MotionMatchingDatabase.format_version`'s meaning — both are serialized
into user `.tres` files, so changing their shape breaks every saved
resource in the wild. `MotionMatchingController.consume_root_motion()`'s
zero-argument signature was already deliberately preserved this session
specifically because it's the one method most likely to be called from
external game-side character-controller scripts every frame.

---

## 6. Semantic Versioning Recommendation

**[INFERENCE]** throughout this section — a release-planning opinion, not a fact.

**v0.x (current → pre-1.0):**
- Fix RB-1 (`project.godot` missing).
- Fix the 3 naming inconsistencies (Section 1) while renaming is free.
- Decide `MMPlaybackMode`'s fate (wire it or remove it).
- Bind the missing `MM_TAG_USER_0/1/2`/`MM_TAG_NONE` constants.
- Get one real, successful build + full test-suite run on a real machine —
  currently the single largest gap between "reviewed" and "released."
- API is expected to still shift during this phase; no compatibility
  promise should be made yet.

**v1.0:**
- Everything in v0.x resolved.
- `MotionMatchingResource`'s property set and `format_version`'s meaning
  frozen (see Section 5's "never change" list).
- `consume_root_motion()`'s signature frozen.
- Public naming fully consistent (`is_X`/`has_X` for all booleans).
- Traversal/motion-warp APIs graduate out of "experimental" only if the
  mid-warp clip-switch question has been explicitly decided (even if the
  decision is "caller's responsibility, documented," as it currently is —
  documenting it clearly counts as resolving it for 1.0 purposes).

**v1.1:**
- Cache size/quantization become resource-tunable (currently fixed
  constants, flagged as a known limitation).
- Database build loop parallelized across clips (currently
  single-threaded despite being structured to allow it).
- Editor debug panel surfaces the new profiler/traversal/warp
  `get_debug_info()` keys.

**v2.0 (only if something in the "never change" list must actually change):**
- Any restructuring of `MotionMatchingResource`'s serialized shape or
  `format_version`'s semantics would need a major version bump, since it
  would require a migration path for existing `.tres` files — nothing
  currently on the roadmap requires this, so v2.0 is speculative rather
  than planned.

---

## 7. Release Checklist

- [ ] **Build verification** — a real compile on at least Linux and one of
  Windows/macOS, using the commands in `docs/HANDOFF_PACKAGE.md` Section 2.
  **Not yet done in any session.**
- [ ] **Runtime verification** — the 18-item plan in
  `docs/HANDOFF_PACKAGE.md` Section 3, at minimum items 1–10 (initialization
  through basic root-motion sanity). **Not yet done.**
- [ ] **Fix RB-1** — add `project.godot` to `demo/`.
- [ ] **Fix the 3 naming inconsistencies** (Section 1 of this document).
- [ ] **Resolve `MMPlaybackMode`** — wire or remove.
- [ ] **Bind missing `MM_TAG_*` constants.**
- [ ] **Documentation** — `docs/` folder already covers architecture,
  workflow, API reference, optimization, and handoff; confirm it's still
  accurate after any pre-1.0 fixes above.
- [ ] **Examples** — `demo/` exists but is not currently a runnable project
  (see RB-1); once fixed, confirm it actually demonstrates the documented
  setup steps in `demo/README.md`.
- [ ] **Tests** — 8 GDScript test scripts exist (`tests/gdscript/`);
  **none have ever been executed.** Running them for the first time belongs
  before, not after, a public release.
- [ ] **CI** — no CI configuration (e.g., GitHub Actions workflow) was
  found anywhere in this repository **[SOURCE — not present]**. For a
  public release, at minimum a build-and-test workflow across
  Linux/Windows/macOS would substitute for the manual verification this
  sandbox cannot perform.
- [ ] **Licensing** — `LICENSE` (MIT) and `AUTHORS` already present
  **[SOURCE]**, confirmed from an earlier session.
- [ ] **Version tags** — no version tag or `CHANGELOG.md` currently exists
  in the repository **[SOURCE — not present]**.
- [ ] **Changelog** — should be created before the first public tag,
  summarizing at minimum: initial feature set, the two verified bugfixes
  from this session (`set_resource()` race, `notify_frame_jump()` dead
  state), and the format_version/validate() additions.
- [ ] **Releases** — no GitHub/other release has been cut
  **[SOURCE — nothing to check locally, but no release artifacts or tags
  exist in this checkout]**. First release should wait until the build
  verification and test-execution items above are actually done on a real
  machine, not just reviewed statically.
