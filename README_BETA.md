# MOTION MATCHING — v0.1.0-beta

A C++ GDExtension implementing motion-matching animation for Godot 4.3+:
searches a precomputed database of animation frames for the best match to
a character's current movement intent, and plays from that exact point —
never restarting a clip from frame zero.

## Status: please read before using

**This addon has never been successfully compiled or run in any
development environment that produced its own documentation.** Everything
described below reflects the design and the source code as written, not
confirmed runtime behavior. See `docs/BETA_RELEASE_AUDIT.md` for the full,
honestly-labeled verification status of every subsystem.

If you're the first person to get a real build working: please open an
issue with your exact compiler/OS/Godot-version/godot-cpp-branch
combination, whether it succeeded, and any errors along the way — this
closes the single biggest gap in the project's current state, for
everyone after you.

## What it does (by design, not yet confirmed at runtime)
- Universal rig detection — works across Mixamo, Unreal, Rokoko,
  ActorCore, Reallusion, Cascadeur, Blender, raw FBX/GLTF, VRM, and
  motion-capture rigs, with no hardcoded bone names.
- Motion-measured clip classification — tags/categories derived from
  actual movement (speed, turn rate, contact pattern), not file names.
- KD-tree accelerated nearest-neighbor pose search, with an optional
  result cache and an optional background search thread.
- Root motion integrated from velocity (not absolute transform deltas),
  so a clip switch never causes a positional pop.
- Optional traversal detection (vault/mantle/climb/slide/roll) and motion
  warping (bending a clip's root motion toward a gameplay target).
- Full IK: foot planting/locking, aim/look, procedural pose warping
  (orientation, stride, lean).
- Editor tooling for the full build pipeline: rig detection, clip
  tagging/review, database building, feature/cost tuning, live debug
  readout.

## Requirements
- Godot 4.3+
- A C++20 compiler
- [godot-cpp](https://github.com/godotengine/godot-cpp) as a submodule,
  matching your Godot version

## Building
See `docs/HANDOFF_PACKAGE.md` for exact per-platform build commands
(Linux/Windows/macOS/Android). **These commands have not been executed
to completion in this project's own development environment** — they are
the standard, expected godot-cpp/SConstruct invocations for this project's
build configuration, documented from source, not from a confirmed run.

## Known limitations (beta)
- No successful build has been performed anywhere yet — this is the
  primary thing a beta tester can help establish.
- `search_completed` signal is declared but never fires — do not depend
  on it.
- `MMPlaybackMode` has no actual effect yet.
- `MMTraversal` and `MMMotionWarp` are experimental: functional by design,
  unverified at runtime, with at least one known unresolved edge case
  (a clip switch occurring mid-motion-warp).
- Three public boolean accessor names are inconsistent with the rest of
  the API and are likely to be renamed before 1.0
  (`get_include_bone_velocity`, `get_include_root_velocity`,
  `get_ground_state` → expected to become `is_*`).
- No example project currently exists in a runnable state.

## Documentation
- `docs/CLASS_REFERENCE_01–04.md` — every public class, in depth
- `docs/SYSTEMS.md` — every major subsystem
- `docs/ARCHITECTURE_AND_WORKFLOW.md` — project structure and the full
  production pipeline
- `docs/EDITOR_GUIDE.md` — the editor tooling
- `docs/BETA_RELEASE_AUDIT.md` — current verification status, honestly
  labeled

## License
MIT — see `LICENSE`.

## Contributing
See `CONTRIBUTING.md`. The single most valuable contribution right now is
a confirmed build report, positive or negative.
