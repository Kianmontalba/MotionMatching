# How to Actually Get a Compiled Build — Using GitHub Actions

This project's source cannot be compiled inside the sandbox that produced
it (confirmed: background build processes are killed when a tool call
ends, before compilation can finish). `.github/workflows/build.yml`
solves this by running the real compile on **GitHub's own servers**
instead, which have real persistent compute.

## Steps

1. Create a new (or use an existing) GitHub repository.
2. Push this entire folder to it:
   ```bash
   cd MotionMatching_v0.1.0-alpha
   git init
   git add .
   git commit -m "Initial alpha"
   git remote add origin <your-repo-url>
   git push -u origin main
   ```
3. Go to your repository on GitHub → the **Actions** tab. The workflow
   ("Build Motion Matching GDExtension") starts automatically on push.
4. Wait for it to finish — Android specifically usually takes 10–20
   minutes on GitHub's runners (compiling godot-cpp's ~939 generated
   binding files is the slow part, same as it would be locally).
5. Click the finished run → scroll to **Artifacts** → download
   `motion_matching-android-arm64` (and any other platforms you need).
6. Unzip the downloaded artifact and place the `.so` files into
   `demo/addons/motion_matching/bin/` in your own project (or wherever
   you've placed the `addons/motion_matching/` folder — the `.gdextension`
   file's paths are now relative to itself, so placement is flexible, per
   the fix in `docs/ANDROID_FIX_PASS.md`).

## If a build fails
The Actions log shows the exact compiler/linker output — this is the
first *real* build log this project will ever have had, whichever way it
goes. If Android fails specifically, check:
- The `GODOT_CPP_BRANCH` value at the top of `build.yml` — set it to
  match your actual Godot version (currently set to `"4.3"`; you're on
  4.7.1, so consider changing this to `"4.7"` if a matching godot-cpp
  branch exists).
- The NDK version (`ndk-version: r23c` in the workflow, corresponding to
  godot-cpp's pinned default `23.2.8568313` — see
  `docs/ANDROID_FIX_PASS.md` Section 9 for why this matters).

## Why this, and not me compiling it directly
Every attempt to run a real build inside this development environment has
been tested and has failed for a structural reason (not a retry-able
one): the sandbox tears down background processes at the end of each
tool-call boundary, before a build of this size can finish, and one such
attempt was directly observed dying mid-run with `ps aux` in a follow-up
call. GitHub Actions runs on a persistent, dedicated machine per job with
no such boundary, which is why it can finish what this environment
cannot.
