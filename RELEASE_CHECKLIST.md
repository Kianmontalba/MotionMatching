# Release Checklist

Nothing on this list has been checked off by execution — every item below
reflects the state as of the last documentation pass. See
`docs/PRE_RELEASE_REVIEW.md` for full evidence behind each line.

## Must fix before any public release
- [ ] Add `demo/project.godot` (RB-1 — currently `demo/` cannot be opened
      as a Godot project at all, despite `demo/README.md` instructing
      exactly that).
- [ ] Perform one real, successful local build on at least one platform
      and record the exact toolchain versions used.
- [ ] Run `godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd`
      for the first time ever and record the pass/fail count for all 8
      scripts.
- [ ] Rename the 3 inconsistent public boolean accessors
      (`get_include_bone_velocity`, `get_include_root_velocity`,
      `get_ground_state` → `is_*`) — free now, a breaking change after
      any tagged release.
- [ ] Decide `MMPlaybackMode`'s fate: wire it into real behavior, or
      remove the enum and its doc comments so it doesn't imply
      functionality that doesn't exist.
- [ ] Add the missing `BIND_ENUM_CONSTANT` calls for `MM_TAG_USER_0/1/2`
      and `MM_TAG_NONE`.

## Should fix before 1.0
- [ ] Confirm or refute the `-ffast-math`/`MM_INFINITY` release-build risk
      with an actual before/after comparison.
- [ ] Confirm Android arm32 is actually supported by the vendored
      godot-cpp branch, or remove those `.gdextension` entries.
- [ ] Stress-test the `set_resource()` race fix specifically (call it
      repeatedly while async search is active and under movement input —
      see `docs/HANDOFF_PACKAGE.md` Section 3, item 12).

## Documentation
- [x] Architecture overview (`docs/architecture.md`,
      `docs/ARCHITECTURE_FULL.md`)
- [x] Full class/node/resource reference (`docs/CLASS_REFERENCE.md`)
- [x] Systems documentation (`docs/SYSTEMS.md`)
- [x] API reference tables (`docs/api.md`)
- [x] Workflow walkthrough (`docs/workflow.md`)
- [x] Optimization guide (`docs/optimization.md`)
- [x] `CHANGELOG.md`, `CONTRIBUTING.md`, this checklist
- [ ] Per-method inline doc comments — currently inconsistent density
      across headers (some classes well-commented per-method, others only
      class-level)

## Examples
- [ ] `demo/` actually opens and runs once `project.godot` is added (see
      "must fix" above) — until then this can't be marked done even
      though the scene/script files themselves exist.

## Tests
- [x] 8 GDScript test scripts exist, covering skeleton profile detection
      (2), clip analysis, database round-trip, pose search, KD-tree vs.
      brute-force equality, root motion continuity, resource validation.
- [ ] All 8 actually executed at least once, pass/fail recorded.
- [ ] Controller-lifecycle-level tests (cache correctness, traversal,
      motion warp, foot contact through a live `SceneTree`) — not yet
      written; would need a different test harness pattern than the
      current `RefCounted`-based one (see `docs/HANDOFF_PACKAGE.md`
      Section 3, item 8's honest gap).

## CI
- [ ] No CI configuration exists in this repository. Minimum
      recommendation: a workflow that builds on Linux/Windows/macOS and
      runs the GDScript test suite headlessly on at least Linux.

## Licensing
- [x] `LICENSE` (MIT) present.
- [x] `AUTHORS` present.
- [x] `CONTRIBUTING.md` states the contribution license.

## Version tags
- [ ] No version tags exist yet. Recommend starting at `v0.1.0` (see
      semver plan in `docs/PRE_RELEASE_REVIEW.md` Section 6) rather than
      jumping straight to `v1.0.0` — the "must fix" list above is not yet
      complete.

## Changelog
- [x] `CHANGELOG.md` created, `[Unreleased]` section populated with this
      project's actual history.
- [ ] Move `[Unreleased]` to a real version section once the "must fix"
      list is complete and a tag is cut.

## Releases
- [ ] No release has been cut. Do not cut one until:
      1. A real build succeeds on a real machine (never done in any
         session to date).
      2. The test suite has actually been run at least once.
      3. The "must fix" list above is complete.
