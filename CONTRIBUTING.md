# Contributing to MOTION MATCHING

## Before you start

This is a C++ GDExtension for Godot 4.3+. Read
`docs/ARCHITECTURE_FULL.md` and `docs/CLASS_REFERENCE.md` first — they
cover folder structure, class relationships, and execution order in
enough detail to orient yourself without reading all 35 source files.

**Known state at time of writing:** this project has never had a
successful local build or test run in any development session that
produced its documentation (see `docs/PRE_RELEASE_REVIEW.md`). If you're
the first person to build it, please open an issue with the exact
compiler/OS/godot-cpp branch combination you used, whether it succeeded,
and paste any errors — this closes the single biggest gap in the
project's current state.

## Ground rules

- **No hardcoded bone names, animation names, or naming conventions
  anywhere in the framework.** The entire point of this addon is
  animation-pack-agnosticism via `MMSkeletonProfile` (structural + name-
  token detection) and `MMClipAnalyzer` (motion-measured classification,
  not name-based). A PR that adds a special case for "Mixamo" or "UE" bone
  names in the core pipeline will be rejected — extend the detection
  system generically instead.
- **No duplicate implementations of an existing system.** This project
  has already had one real regression from a consolidation (rig detection
  used to have two competing systems; see `CHANGELOG.md`'s "Earlier
  history"). If you find yourself writing a second version of something
  that already exists, that's a sign to extend the existing one.
- **C++20**, matching the existing `SConstruct`/`CMakeLists.txt` flags.
- **Follow the existing `MM_ACCESSORS`/`MM_BIND_PROPERTY` macro patterns**
  (see `include/mm_types.hpp`) for new simple accessors — don't hand-write
  boilerplate that the macros already cover.
- **Naming:** boolean accessors are `is_X()`/`has_X()`, not `get_X()` —
  this project has 3 known exceptions (see `CHANGELOG.md`'s "Known
  issues") that are considered defects, not precedent to follow.

## Testing

8 GDScript test scripts exist in `tests/gdscript/`, run via:
```
godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd
```
**As of this writing, none of them have ever been executed.** Running
them for the first time, on whatever platform you're building on, is one
of the most valuable things a new contributor can do. If you add a new
class or fix a bug, add a corresponding test to this suite following the
existing pattern (`extends RefCounted`, a `run() -> bool` method,
registered in `run_tests.gd`'s `TEST_SCRIPTS` array) — see
`test_kdtree_vs_bruteforce.gd` or `test_resource_validation.gd` for
recent examples.

## Before submitting a PR

1. Build locally on at least one platform (see `docs/HANDOFF_PACKAGE.md`
   Section 2 for exact commands per platform).
2. Run the full test suite and confirm the count of passing/failing
   tests in your PR description — "tests pass" claims without this are
   not verifiable by reviewers who haven't built it themselves either.
3. If you touch anything in `src/thread_pool.cpp` or
   `src/cache_manager.cpp` (the only threaded code in the project),
   explain the threading reasoning explicitly in the PR — this is the one
   area where a subtle race has already been found and fixed once (see
   `CHANGELOG.md`).
4. Update `docs/api.md`/`docs/CLASS_REFERENCE.md` if you change a public
   (bound) method's signature or behavior.
5. Add a `CHANGELOG.md` entry under `[Unreleased]`.

## Reporting issues

Please label whether your report is based on:
- an actual build/run you performed (include compiler, OS, Godot version,
  godot-cpp branch), or
- reading the source and suspecting a problem.

This project's own internal review process (see `docs/PRE_RELEASE_REVIEW.md`)
distinguishes these explicitly, and issue reports that do the same are
much faster to triage.

## License

MIT — see `LICENSE`. By contributing, you agree your contribution is
licensed under the same terms.
