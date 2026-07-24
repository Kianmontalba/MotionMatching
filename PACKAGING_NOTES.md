# Packaging Notes — v0.1.0-alpha

Two items from a generic GDExtension packaging checklist are
**intentionally absent** from this package. Noting why here so their
absence doesn't look like an oversight during your first build.

## `plugin.cfg` — intentionally not included
This addon is a **self-registering GDExtension**
(`motion_matching.gdextension` + `register_types.cpp`'s
`EditorPlugins::add_by_type<MotionMatchingEditorPlugin>()`), not a
GDScript-based editor plugin. A `plugin.cfg` is the manifest format for
the latter (it points at a `.gd` script as its entry point); this addon
has no such script, so adding a `plugin.cfg` would either do nothing or
actively break Godot's plugin loader if it referenced a nonexistent
script. This was a deliberate architectural decision made earlier in this
project's history, not an omission — freezing the repository "exactly as
it currently exists" means preserving that decision, not adding a file to
satisfy a generic checklist item that doesn't apply to this addon's
actual design.

## `SCsub` — intentionally not included
This project's build entry point is the root `SConstruct`, which globs
`src/*.cpp` and `editor/*.cpp` directly rather than delegating to
per-directory `SCsub` files. This is a valid, simpler alternative SCons
pattern for a project of this size (one source tree, not many
independently-buildable submodules) and was the pattern already in place.
No `SCsub` currently exists anywhere in this repository outside the
vendored `godot-cpp` dependency (which manages its own build separately).

## Icons — none exist
No custom class icons (`.svg`) have been authored for any of the 28
registered classes. They will use Godot's default type icons in the
editor. This is a cosmetic gap, not a functional one — nothing in this
repository was found to reference or expect custom icon files.

## `godot-cpp/` — deliberately not bundled in this zip
This is a vendored **git submodule**, not part of this addon's own
source. A zip archive can't carry a submodule reference the way a git
clone can, so it's excluded here rather than included as a stale/wrong
snapshot. **Before building, fetch it yourself**, matching your Godot
version:
```bash
git clone --branch <your-godot-version-branch> https://github.com/godotengine/godot-cpp godot-cpp
```
placed at the repository root (`godot-cpp/`, alongside `SConstruct`).
If you obtained this project via `git clone` instead of this zip, use
`git submodule update --init --recursive` instead, per `README.md`.

## Everything else in the requested package contents
Is present and included in the zip as-is: `include/`, `src/`, `editor/`,
`docs/`, `tests/`, `demo/` (containing the actual installable addon at
`demo/addons/motion_matching/`, with `motion_matching.gdextension` and
the empty `bin/` output directory), `LICENSE`, `AUTHORS`, `README.md`,
`CHANGELOG.md`, `SConstruct`, `CMakeLists.txt`, plus every other
documentation and process file produced across this project's alpha/beta/
RC audit history.
