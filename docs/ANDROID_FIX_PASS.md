# MOTION MATCHING — Android Runtime Fix Pass (Godot 4.7.1)

Focused entirely on the reported failure. Three real defects were found
and fixed in the repository during this pass (not just reported — see
"Fixes Applied" below). Everything else is root-cause analysis and exact
instructions for what you need to do on your own machine, since the
actual `.so` build/export step happens outside this environment.

---

## 1. Root Cause Analysis

The primary error —
```
ERROR: Can't open dynamic library: libmotionmatching.android.template_debug.arm64.so
dlopen failed: library "libmotionmatching.android.template_debug.arm64.so" not found
```
— means Godot looked for that exact file at the path declared in
`motion_matching.gdextension` and it wasn't there **at runtime on the
Android device**. This has one of three causes, ranked by likelihood:

**Cause A (most likely): the Android `.so` was never actually built.**
This project's entire prior history (every audit performed before this
one) confirms zero successful builds on any platform, ever. If the only
thing that got built so far was, e.g., a desktop platform for editor
testing, the Android-specific build
(`scons platform=android target=template_debug arch=arm64`) simply never
ran, so `demo/addons/motion_matching/bin/libmotionmatching.android.template_debug.arm64.so`
does not exist on disk at all. Godot then either shows a placeholder in
the editor (desktop) or fails exactly like your log shows (device).

**Cause B (very common, well-documented, independent of Cause A): the
`.so` exists in `bin/` but Godot's Android export never bundled it.**
Godot's exporter only auto-includes files that are imported as Godot
*resources* — native libraries (`.so`/`.dll`/`.dylib`) are not resources
and are **not** included in an export by default. For GDExtension native
libraries to be bundled into an Android build at all, the Android export
preset must have **Custom Build (Gradle) enabled** — the non-custom,
precompiled Android export template cannot embed arbitrary native
GDExtension libraries. If Custom Build isn't enabled in your Android
export preset, this exact error is the expected result even with a
perfectly good `.so` sitting in `bin/`.

**Cause C (a real, verified toolchain pitfall in godot-cpp itself): NDK
version pinning.** Read directly from
`godot-cpp/tools/android.py`: the default `ndk_version` is
`23.2.8568313` — a *specific, pinned* NDK version. If your `ANDROID_HOME`
is set but does not have exactly that NDK side-by-side version installed
(very common if you installed a newer NDK via Android Studio's SDK
Manager), `get_android_ndk_root()` silently constructs a path to an NDK
that doesn't exist, which either fails the build outright with an
obscure toolchain error, or — if you're using `ANDROID_NDK_ROOT` directly
instead of `ANDROID_HOME` — bypasses this pinning but may then use a
different, uncertain NDK/compiler combination than what this godot-cpp
branch expects.

**What is *not* the cause, confirmed by reading the actual build
configuration:** the filename pattern itself. Traced directly through
`godot-cpp/tools/godotcpp.py` line 546:
```python
suffix = ".{}.{}".format(env["platform"], env["target"])
...
suffix += "." + env["arch"]
env["suffix"] = suffix
```
For `platform=android, target=template_debug, arch=arm64`, this produces
`.android.template_debug.arm64`. Combined with this project's own
`SConstruct` (`"libmotionmatching" + suffix + SHLIBSUFFIX`), the result
is exactly `libmotionmatching.android.template_debug.arm64.so` — which
**exactly matches** what `motion_matching.gdextension` declares. The
naming configuration in this repository is correct; it was not the bug.

---

## 2–4. SConstruct / Android ARM64 Target / Filename Verification

**Verified correct, no fix needed.** The relevant branch of `SConstruct`:
```python
else:
    library = env.SharedLibrary(
        "demo/addons/motion_matching/bin/libmotionmatching{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )
```
correctly falls through to this generic branch for `platform=android`
(only macOS and iOS have special-cased branches above it), and produces
the exact filenames both requested build targets need:
- `libmotionmatching.android.template_debug.arm64.so`
- `libmotionmatching.android.template_release.arm64.so`

**`godot-cpp/tools/android.py` confirms `arch` must be explicitly one of
`arm64`, `x86_64`, `arm32`, `x86_32`** — there is no silent default that
would produce a differently-named file; an unsupported value causes an
explicit `env.Exit(1)`. This means **you must always pass `arch=arm64`
explicitly** — omitting it is a real, easy mistake, not a "reasonable
default" godot-cpp will guess for you.

---

## 5. `.gdextension` File Verification

**One real defect found and fixed:** the `[dependencies]` section
declared empty dependency dicts for `android.debug.arm64` and
`android.release.arm64`, but `[libraries]` also declares
`android.debug.arm32`/`android.release.arm32` entries with **no matching
`[dependencies]` entries at all**. Fixed — both arm32 entries added as
empty dicts (correct, since this addon has no extra shared-library
dependencies beyond its own single `.so`).

**Everything else in the file, verified correct:**
- `entry_symbol = "motion_matching_library_init"` matches the `extern "C"`
  function name in `register_types.cpp` exactly.
- `compatibility_minimum = "4.3"` — satisfied by Godot 4.7.1 (4.7.1 ≥ 4.3).
- All declared paths use `res://addons/motion_matching/bin/...`, matching
  where both `SConstruct` and `CMakeLists.txt` place their output.

---

## 6. Install Location Verification

**Verified correct.** `SConstruct` targets
`demo/addons/motion_matching/bin/` for every platform; `CMakeLists.txt`
sets the same path via `LIBRARY_OUTPUT_DIRECTORY`/`RUNTIME_OUTPUT_DIRECTORY`.
This matches exactly what `motion_matching.gdextension`'s `res://` paths
expect, since `demo/` is (now, after this pass's fix below) a real Godot
project root, making `res://addons/motion_matching/bin/` resolve to
`demo/addons/motion_matching/bin/` on disk.

**One real defect found and fixed:** `demo/` had **no `project.godot`
file at all** — meaning it could never be opened as a Godot project, on
any platform, Android or otherwise, until this pass. This has been
flagged in every audit produced for this project going back to the very
first pre-build review and was never fixed until now. **Fixed:** a
minimal `demo/project.godot` has been created, defining the main scene
(`demo_scene.tscn`) and the 6 input actions `player.gd` reads
(`move_left`/`move_right`/`move_forward`/`move_back`/`sprint`/`jump`),
with `renderer/rendering_method="mobile"` set given this is explicitly
being targeted at Android.

---

## 7. Android Export Packaging

**This is the step most likely to need action on your end, and it can't
be verified from source alone** — it depends on your Android export
preset configuration in the Godot editor, which isn't part of this
repository.

**Required, per Cause B above:**
1. Project → Export → Android preset → confirm **"Gradle Build" / "Use
   Custom Build" is enabled.** Without this, native GDExtension `.so`
   files are never bundled, regardless of whether they exist in `bin/`.
2. Confirm the Android SDK, a matching NDK, and Gradle/JDK are correctly
   configured in Godot's Editor Settings → Export → Android, and that
   Godot's own Android build template has been installed
   (Project → Install Android Build Template, if using Custom Build).
3. Confirm the export preset's target architecture includes `arm64-v8a`
   (matching what you built), and that your test device is actually
   arm64 (nearly all modern devices are, but worth confirming if you're
   using an emulator, which may default to x86_64).

---

## 8. Godot 4.7.1 Compatibility

**[Not independently re-verified against this specific version's release
notes in this pass]** — `compatibility_minimum = "4.3"` in the
`.gdextension` file means Godot 4.7.1 should accept this extension's
version declaration without complaint, and nothing in this addon uses
any Godot API that would be expected to have changed in a
backward-incompatible way between 4.3 and 4.7.1 based on this addon's own
API surface (standard `Node`/`Resource`/`RefCounted`/`SkeletonModifier3D`
usage throughout). If Godot 4.7.1 rejects the extension for a version
reason, the actual editor/log message would say so explicitly and
differently from the dlopen error you're seeing — your reported error is
specifically about the *file not being found*, not about a version
mismatch, which is consistent with Cause A or B above, not a 4.7.1
compatibility problem.

---

## 9. godot-cpp Version Compatibility

Confirmed from source: this project's godot-cpp checkout pins
`ndk_version = "23.2.8568313"` as its default (see Cause C). **Action
required on your end:** confirm which NDK version you actually have
installed and either (a) install NDK `23.2.8568313` side-by-side via
Android Studio's SDK Manager to match this default exactly, or (b) pass
`ndk_version=<your installed version>` explicitly on the `scons` command
line if you're intentionally using a different one. Also confirm your
`godot-cpp` submodule branch matches your actual Godot version (4.7.x) —
if this checkout's `godot-cpp` is still on an older branch (e.g., `4.3`,
per this project's earlier session history), that mismatch alone can
cause subtle ABI issues even when the file loads. **Re-clone/re-checkout
`godot-cpp` on the branch matching 4.7 before rebuilding.**

---

## 10. Are Android Builds Actually Implemented, or Only Documented?

**Answer, stated plainly: only documented, until you run the build
yourself.** The `SConstruct` code that *would* produce a working Android
build is present and — per items 2–4 above — verified correct on paper.
But no session in this project's history has ever executed
`scons platform=android target=template_debug arch=arm64` to completion,
and your reported error is fully consistent with that never having
happened successfully yet on your machine either (Cause A), or having
happened but not being export-bundled (Cause B). This pass's job was to
make sure the *configuration* has no known defect blocking a first real
success — that is now true, to the extent source review can confirm it.

---

## Secondary File Errors — Findings and Fixes

**`character.tscn` → `res://motion_matching.tres` — real, fixed.** This
file has never existed anywhere in this repository at any point in its
history (confirmed by direct filesystem search). `demo/README.md`'s own
step 6 says to "assign the saved database to `motion_matching.tres`" —
meaning this file was always meant to be *created by you* after building
a database, never shipped. Its presence as a hard `ext_resource`
dependency in `character.tscn` meant the scene could never load at all
until that file existed. **Fixed:** the `ext_resource` and its usage on
the `MotionMatchingController` node have been removed; `resource` is now
left unassigned, matching the documented workflow — build your database,
then assign it to the node yourself (in the editor or via a small script)
before running.

**`demo/project.godot` — real, fixed.** See item 6 above.

**`synthetic_skeleton.gd` (and every other test file's identical
pattern) — real defect, *not* fixed, needs your decision.** Every test
script does:
```gdscript
const SyntheticSkeleton = preload("res://../tests/gdscript/synthetic_skeleton.gd")
```
The `res://../` pattern escapes the project root by one directory. This
only works when Godot is running directly against a real folder on disk
(exactly the documented test invocation: `godot --headless --path demo/
--script ../tests/gdscript/run_tests.gd`, where `res://` = `demo/` and
`demo/../` = the repository root, a real directory). **This pattern
cannot work inside any packaged/exported build** — an exported APK's
`res://` is a self-contained virtual filesystem with no real parent
directory to escape to. This is not currently causing your reported
error (the primary error is about the addon's own `.so`, not the test
suite, and the test suite is not part of what ships in an Android
export), but it means: **do not attempt to run
`tests/gdscript/run_tests.gd` from inside an exported Android build** —
it will fail there by design, and needs a different distribution strategy
(e.g., copying `synthetic_skeleton.gd` under `demo/` directly) if you
ever want tests runnable from a packaged build rather than only from the
editor/source checkout. Left as-is for now since fixing it would mean
restructuring the test suite's file layout, which is a larger decision
than this fix pass's scope.

**`player.gd` — no defect found.** Cross-checked every method it calls on
`MotionMatchingController`
(`request_jump`, `set_fall_distance`, `set_velocity`, `set_facing`,
`set_direction`, `set_ground_state`, and the `animation_tree_path`
property `character.tscn` sets) directly against `_bind_methods()` in
`src/motion_matching.cpp` — **every single one exists and is correctly
bound.** This is worth flagging explicitly: earlier documentation
produced for this project (`docs/production/CLASS_REFERENCE_01.md`)
under-documented this class's real API surface, listing only
`set_desired_velocity`/`set_desired_facing` and missing this whole
parallel, already-implemented convenience API
(`set_velocity`/`set_facing`/`set_direction`/`set_ground_state`/
`set_fall_distance`/`request_jump`/`animation_tree_path`). That
documentation gap is noted here for the record; it is not a runtime bug
in `player.gd`, which was already calling real, correctly-bound methods.

---

## Correct Android Build Commands

```bash
# One-time, matching your actual Godot version's branch:
git clone --branch 4.7 https://github.com/godotengine/godot-cpp godot-cpp
cd godot-cpp

# Confirm your installed NDK version matches, or override it explicitly:
export ANDROID_HOME=/path/to/your/Android/Sdk
scons platform=android target=template_debug arch=arm64 ndk_version=<your NDK version if not 23.2.8568313>
cd ..

# Build the addon itself, same arch/target:
scons platform=android target=template_debug arch=arm64
scons platform=android target=template_release arch=arm64
```
Confirm both commands produce the expected output:
```
demo/addons/motion_matching/bin/libmotionmatching.android.template_debug.arm64.so
demo/addons/motion_matching/bin/libmotionmatching.android.template_release.arm64.so
```
before attempting to run on-device or export.

---

## Correct Folder Structure (verified/fixed this pass)
```
demo/
├── project.godot                          <- FIXED: was missing entirely
├── demo_scene.tscn
├── character.tscn                          <- FIXED: broken ext_resource removed
├── player.gd
└── addons/
    └── motion_matching/
        ├── motion_matching.gdextension     <- FIXED: arm32 deps added
        └── bin/
            ├── libmotionmatching.android.template_debug.arm64.so    <- must exist (Cause A)
            └── libmotionmatching.android.template_release.arm64.so  <- must exist (Cause A)
```

## Correct Packaging / Export Structure
For the Android export specifically, confirm (outside this repository,
in the Godot editor's Export dialog, per item 7):
- Custom Build / Gradle Build: **enabled**
- Target architecture: **arm64-v8a** included
- Android Build Template: **installed** (if using Custom Build)
- SDK/NDK/JDK paths: **configured and matching what you built with**

## Final Fixed Android-Ready Addon Layout
The three real defects found in this repository have been fixed
directly (`demo/project.godot` created, `character.tscn`'s broken
reference removed, `.gdextension`'s arm32 dependencies added). What
remains is entirely on your machine: build the two `.so` files with the
exact commands above, confirm your Android export preset has Custom
Build enabled, and confirm your NDK version. Given the analysis above,
**Cause A or Cause B — not a configuration defect in this repository —
is the most probable explanation for your reported error**, and both are
things only you can resolve by actually running the build and checking
your export preset.
