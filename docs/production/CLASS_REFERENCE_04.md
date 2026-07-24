# MOTION MATCHING — Production Class Reference (Part 4 of 4)

Continued from Part 3. Same evidence labeling convention.

---

# MMFootIKModifier (`SkeletonModifier3D`, a `Node3D`)

**1. Purpose:** ground adaptation — stops feet floating on stairs, sinking
into slopes, and over-extending the standing leg; locks a planted foot in
place for exactly as long as the source animation had it planted.

**2. Overview:** resolves 7 bone names once (both legs, pelvis), then
each tick raycasts under each foot, analytically solves each leg to the
adjusted ground height, adjusts the pelvis so the standing leg never
over-extends, and honors lock state.

**3. Internal Workflow:** order matters — the pelvis is lowered first so
both legs solve against a reachable target; solving the legs first and
then moving the pelvis would undo the solve (this ordering is stated
explicitly in the implementation's own comment).

**4. Runtime Behavior:** `_process_modification()` runs every tick the
skeleton modifier system processes it (standard `SkeletonModifier3D`
lifecycle **[EXPECTED]**).

**5. When to Use:** any humanoid character walking on non-flat ground.

**6. When NOT to Use:** flying/swimming characters, or characters whose
feet never touch a walkable surface.

**7. Inputs:** a `Skeleton3D` (via the modifier base), a physics world
with raycastable ground geometry, per-tick `set_foot_contacts(left, right)`
calls fed from `MotionMatchingController::get_debug_info()`'s
`left_foot_contact`/`right_foot_contact` keys.

**8. Outputs:** modified bone poses (legs, pelvis) written directly to
the skeleton.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `left_upper_leg`/`left_lower_leg`/`left_foot` | `String` | "LeftUpperLeg"/"LeftLowerLeg"/"LeftFoot" | Bone names (Mixamo-style defaults) |
| `right_upper_leg`/`right_lower_leg`/`right_foot` | `String` | mirrored | Bone names |
| `pelvis` | `String` | "Hips" | Pelvis bone name |
| `ray_start_height` | `float` | 0.6 | Raycast start height above the foot |
| `ray_length` | `float` | 1.2 | Raycast length |
| `foot_offset` | `float` | 0.02 | Small vertical offset above the hit point |
| `max_step_height` | `float` | 0.55 | Maximum adjustment allowed in one step |
| `pelvis_adjust_strength` | `float` | 1.0 | How much the pelvis compensates |
| `smoothing_halflife` | `float` | 0.08 | Smoothing rate for the adjustment |
| `adapt_rotation` | `bool` | true | Whether foot rotation also adapts to slope |
| `max_slope_angle` | `float` | 0.87 rad (~50°) | Steepest slope the adaptation will follow |
| `collision_mask` | `int` | 1 | Physics layer mask for the ground raycasts |

**10. Methods**
| Method | Description |
|---|---|
| `set_foot_contacts(left, right)` | Drives lock state from database contact flags — the critical, previously-broken data path (fixed in an earlier session) |
| `_process_modification()` | Per-tick solve (protected/override, not called directly by users) |

**Common mistakes [SOURCE, from this session's added diagnostic]:** bone
name properties not matching the actual skeleton — now surfaced via a
`WARN_PRINT_ONCE` at first failed resolution rather than silently doing
nothing.

**11. Signals:** none.

**12. Dependencies:** requires a `Skeleton3D`, physics ground geometry,
and (for locking specifically) real per-frame contact data from a
`MotionMatchingController`. Uses `MMIKSolver::solve_two_bone()`
internally.

**13. Data Flow:** database contact bits → controller debug info →
gameplay script → `set_foot_contacts()` → lock state + raycasts →
two-bone solve → pelvis adjustment → skeleton pose.

**14. Execution Order:** runs as part of the skeleton's modifier
pipeline, after the animation itself has been applied but before the
final pose is presented **[EXPECTED, standard `SkeletonModifier3D`
ordering]**.

**15. Performance Notes:** 2 raycasts/tick when both leg chains resolve;
analytic (non-iterative) leg solve — cost is constant regardless of
distance to the ground.

**16. Thread Safety:** main-thread only (physics raycasts, skeleton pose
writes).

**17. Serialization:** exported properties save with the scene.

**18. Limitations:** requires the 7 configured bone names to match the
actual skeleton; a mismatch is now warned about (once) rather than
failing silently, but still requires the user to fix the names manually.

**19. Best Practices:** verify bone names against your actual rig before
relying on this modifier — check the console for the new
`WARN_PRINT_ONCE` diagnostic on first run with a new skeleton.

**20. Example:**
```gdscript
func _physics_process(_delta):
    var info = controller.get_debug_info()
    foot_ik.set_foot_contacts(info["left_foot_contact"], info["right_foot_contact"])
```

**21. Related Classes:** `MMIKSolver` (solver it uses),
`MotionMatchingController` (contact data source).

---

# MMAimIKModifier (`SkeletonModifier3D`)

**1. Purpose:** distributes an aim/look rotation across a bone chain with
per-bone weights and a cone-angle limit, so the character can't visually
break its own neck.

**3. Internal Workflow:** smooths toward `target` (halflife-based), then
rotates the configured chain toward it, respecting `max_angle`. Runs
after `MMWarpModifier` by design, per its own doc comment, so it aims
from the already orientation-corrected pose.

**5. When to Use:** aiming, look-at, camera-facing head tracking.

**6. When NOT to Use:** anywhere a full IK chain to a *position* (not
just a direction) is needed.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `chain_bones` | `PackedStringArray` | — | The bone chain to distribute rotation across |
| `bone_weights` | `PackedFloat32Array` | — | Per-bone weight (parallel array to `chain_bones`) |
| `target` | `Vector3` | — | World-space aim target |
| `forward_axis` | `Vector3` | (0,0,-1) | Which local axis is "forward" for each bone |
| `max_angle` | `float` | 1.2 rad | Cone limit |
| `smoothing_halflife` | `float` | 0.06 | Smoothing rate |

**10. Methods:** `set_chain_bones()`/`get_chain_bones()`, standard
`MM_ACCESSORS` for the properties above, `_process_modification()`
(override).

**12. Dependencies:** requires a `Skeleton3D`; conceptually paired with
`MMWarpModifier` (ordering matters).

**16. Thread Safety:** main-thread only.

**21. Related Classes:** `MMWarpModifier` (runs before this),
`MMIKSolver::look_at_bone()` (related single-bone solver — confirm
against source before assuming this class delegates to it directly for
chain distribution).

---

# MMWarpModifier (`SkeletonModifier3D`)

**1. Purpose:** pose-level warping applied *after* the animation has
already been sampled — orientation warping (running at an angle without a
dedicated diagonal clip), stride warping (step length scales with actual
speed — "the single biggest cause of foot sliding" per its own comment),
and procedural upper-body lean in response to acceleration.

**6. When NOT to Use:** don't confuse with `MMMotionWarp` (a `RefCounted`
subsystem operating on the root motion *delta* before it's applied) —
this class operates on the already-sampled *pose*. They're complementary,
not overlapping; using one doesn't replace the other.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `spine_bones` | `PackedStringArray` | — | Spine chain for counter-rotation |
| `pelvis_bone` | `String` | "Hips" | Pelvis bone |
| `left_foot_bone`/`right_foot_bone` | `String` | "LeftFoot"/"RightFoot" | Foot bones |
| `orientation_angle` | `float` | 0.0 | Desired orientation offset from the clip's authored forward |
| `orientation_blend` | `float` | 1.0 | Blend strength |
| `spine_counter_rotation` | `float` | 0.5 | How much the spine counter-rotates against orientation warping |
| `stride_scale` | `float` | 1.0 | Step-length scale factor |
| `lean_amount` | `float` | 0.0 | Procedural lean strength |
| `lean_axis` | `Vector3` | (1,0,0) | Lean rotation axis |
| `smoothing_halflife` | `float` | 0.08 | Smoothing rate |

**12. Dependencies:** requires a `Skeleton3D`; runs before
`MMAimIKModifier` in the intended pipeline order.

**21. Related Classes:** `MMAimIKModifier` (runs after this),
`MMMotionWarp` (complementary, root-motion-level warping — not the same
system).

---

# MMDebugDraw (`MeshInstance3D`)

**1. Purpose:** visualizes the trajectory predictor and the matched
clip's own trajectory in the 3D viewport at runtime.

**3. Internal Workflow:** draws past trajectory (behind), predicted
future trajectory (ahead), a marker per sample point, and — if enabled —
the matched clip's own trajectory on top, so a mismatch between predicted
and actual motion is visible rather than merely suspected.

**6. When NOT to Use:** in shipped/release builds — this does per-frame
immediate-mode mesh rebuilding, a real cost not worth paying once tuning
is done. **[INFERENCE]**

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `controller_path` | `NodePath` | — | Which controller to visualize |
| `draw_trajectory` | `bool` | true | Toggle predicted/history line |
| `draw_matched_trajectory` | `bool` | true | Toggle matched-clip overlay |
| `draw_facing` | `bool` | true | Toggle facing direction indicator |
| `marker_size` | `float` | 0.06 | Sample point marker size |
| `history_color` | `Color` | blue-ish (0.35, 0.55, 1.0) | Past trajectory color |
| `future_color` | `Color` | green-ish (0.2, 1.0, 0.5) | Predicted trajectory color |
| `matched_color` | `Color` | orange-ish (1.0, 0.65, 0.15) | Matched clip trajectory color |

**16. Thread Safety:** main-thread only (rendering).

**21. Related Classes:** `MotionMatchingController` (data source),
`MMTrajectory` (the data being visualized).

---

# AnimationNodeMotionMatching (`AnimationRootNode`)

**1. Purpose:** the `AnimationTree` integration point — drop this in as
the tree root (or inside a `BlendTree`) and the tree plays whatever the
bound controller decided that tick.

**3. Internal Workflow:** blends exactly two clips (the one being left,
the one being entered) using `blend_animation()` with an *absolute* time
argument — the specific Godot `AnimationTree` API detail that lets a
match start at, e.g., 4.7 seconds into a clip instead of at zero, which
per its own comment is "the whole point of motion matching and the thing
a state machine cannot do."

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `controller_path` | `NodePath` | — | Which controller drives this node |
| `use_custom_timeline` | `bool` | true | Whether this node manages its own timeline rather than deferring to the tree's |

**10. Methods**
| Method | Description |
|---|---|
| `bind_controller(controller)` | Called by the controller in its own `_ready()`, so this node never has to search the scene tree during animation processing |
| `_get_caption()` (override) | Editor display name |
| `_has_filter()` (override) | Standard `AnimationNode` override |
| `_process_animation_node(time, seek, is_external_seeking, test_only)` | Core per-tick blend logic |
| `_process(...)` (override) | Standard `AnimationRootNode` entry point |

**12. Dependencies:** requires a bound `MotionMatchingController`.

**16. Thread Safety:** main-thread only, called as part of the
`AnimationTree`'s own processing.

**17. Serialization:** saved as part of the `AnimationTree`'s tree
structure (it's an `AnimationNode`/`Resource`), not independently.

**21. Related Classes:** `MotionMatchingController` (its data source).

---

# Editor Tools (`TOOLS_ENABLED` only — not present in exported games)

## MMDatabaseEditor (`VBoxContainer`)
**Purpose:** build, inspect, and save a motion database from the editor.
The clip table is the important part — "with 800 clips, tagging is the
job, and doing it in the inspector one resource at a time is not a
workflow" per its own comment. Generates: a `MotionMatchingDatabase`
resource (via the standard scan → build → save workflow).

## MMFeatureEditor (`VBoxContainer`)
**Purpose:** edits schema layout and cost weights. Weight sliders are
normalized to a percentage of total cost so the numbers mean what a
designer expects, rather than raw unbounded weight values.

## MMTrajectoryEditor (`VBoxContainer`)
**Purpose:** tunes the predictor's halflife parameters and explains what
each one does to the feel — "the parameter set most likely to be tuned by
someone who did not write the code" per its own comment.

## MMDebugTools (`VBoxContainer`)
**Purpose:** a live profiler readout for the selected controller while
the game runs (via the editor's remote debugging connection).

## MotionMatchingEditorPlugin (`EditorPlugin`)
**Purpose:** adds one bottom panel hosting all four tools above, so the
whole authoring workflow lives in one place instead of across four
separate inspectors.

**Note on editor tool detail [INFERENCE]:** exact per-button/per-slider
behavior was not re-verified line-by-line in this documentation pass —
the four purpose statements above are sourced directly from each class's
own header comment, but a full enumeration of every individual UI control
would require re-reading each `.cpp` implementation file in detail, which
this pass did not repeat beyond what earlier sessions already covered.
Treat the per-tool descriptions above as accurate at the "what this panel
is for" level, and the underlying `.cpp` files as the authoritative
source for exact control-by-control behavior.
