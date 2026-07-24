# Demo scene

This folder is a minimal Godot project layout, not a finished game. It ships
the wiring, not the art: drop your own rig and animation library in and the
scenes work as they are.

## Setup

1. Build the extension (see the root README), so
   `demo/addons/motion_matching/bin/` contains a library for your platform.
2. Open `demo/` as a Godot project.
3. Add your character mesh under `Player/Rig`, and make sure its skeleton is
   the node named `GeneralSkeleton`.
4. Assign your `AnimationLibrary` to the `AnimationTree`.
5. Open the **Motion Matching** bottom panel, point it at the skeleton and the
   library, press **Scan**, then **Build database**, then **Save**.
6. Assign the saved database to `motion_matching.tres`.
7. Set `AnimationTree.tree_root` to a new `AnimationNodeMotionMatching`.

## Input actions

The demo script expects these actions in the project input map:

`move_left`, `move_right`, `move_forward`, `move_back`, `sprint`, `jump`
