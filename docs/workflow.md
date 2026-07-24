# MOTION MATCHING — Workflow

This walks through taking an animation library from any source and getting a
character moving with it.

## 1. Import your animations

Import your model as usual (FBX/GLTF/Blend) so Godot produces a `Skeleton3D`
and an `AnimationLibrary`. No renaming, retargeting, or naming convention is
required — Mixamo exports, Unreal-authored skeletons, mocap solves, and
custom studio rigs all work unmodified.

## 2. Open the Motion Matching panel

With the addon enabled (`res://addons/motion_matching`), a **Motion
Matching** tab appears in the bottom editor panel with four sub-tools:
Database, Feature, Trajectory, Debug.

## 3. Build the database

In the **Database** tab:

1. Set **Skeleton Path** to your character's `Skeleton3D`.
2. Set **Library Path** to the `AnimationLibrary` (or resource) containing
   your clips.
3. Click **Scan**. The panel runs `MMSkeletonProfile::auto_detect()` and
   reports which bone roles were found and which (if any) are missing. A
   humanoid rig should resolve hips, both feet, spine, and head at minimum.
4. Click **Validate** to check the library for clips with no root motion
   track, zero-length clips, or bones referenced by the schema but missing
   from the skeleton.
5. Review the auto-generated clip tags in the table. These come from
   `MMClipAnalyzer` measuring each clip's motion — adjust any you disagree
   with, or leave them; motion-based tags are usually correct even for an
   unfamiliar animation pack.
6. Click **Build**. This runs `MMFeatureExtractor` over every clip and
   produces a `MotionMatchingDatabase` resource.
7. Click **Save** and choose a path (commonly next to your character scene).

## 4. Configure the feature schema

In the **Feature** tab, tune:

- **Trajectory times** — how far ahead/behind the predicted trajectory
  samples (defaults: 0.2s, 0.4s, 0.6s, 1.0s cover most locomotion).
- **Pose bones** — auto-filled from the skeleton profile; add hands if your
  game needs upper-body matching (weapon holds, climbing).
- **Cost weights** — shown as a percentage of the total; raise trajectory
  weight for a more responsive character, raise pose weight for a smoother
  one.

## 5. Wire up the scene

1. Add a `MotionMatchingController` node under your character.
2. Assign its `database` to the `.tres` you built.
3. Add an `AnimationTree` with an `AnimationNodeMotionMatching` node inside
   its blend tree; the controller finds and binds to it automatically at
   `_ready()`.
4. From your character script, feed the controller every frame:

```gdscript
func _physics_process(delta: float) -> void:
    mm_controller.set_desired_velocity(desired_velocity)
    mm_controller.set_facing(facing_direction)
    mm_controller.set_ground_state(is_on_floor())
    if Input.is_action_just_pressed("jump"):
        mm_controller.request_jump()
```

5. If you're using foot IK or motion warping, add `MMFootIKModifier` /
   `MMWarpModifier` as `SkeletonModifier3D` children of your `Skeleton3D` —
   they read base rotations from the editor at `_ready()`, so no bone-name
   configuration is needed there either.

## 6. Debug and profile

The **Debug** tab polls a running `MotionMatchingController` at 4Hz and
prints the matched clip, playback time, blend weight, match cost, and
search-worker statistics — use it to confirm the character is matching
sensibly before chasing a gameplay bug that might just be a bad tag.

`MMProfiler` (attached automatically to the controller) tracks search time
percentiles and counts clip switches and budget overruns; read them via the
controller's `get_profiler()` accessor for an in-game overlay if needed.

## 7. Iterate

Because tags and the database are derived from measured motion, adding a new
animation pack later is: import it into the same `AnimationLibrary` (or a
new one merged with `MMAnimationLibraryTools::merge_libraries()`), re-run
Scan/Validate/Build, and the new clips slot into the same tag/category
system automatically.
