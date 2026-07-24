# MOTION MATCHING — Complete Class Reference

Every class registered in `register_types.cpp` (28 total, confirmed
**[STATIC]** by script cross-check against `GDCLASS`/`GDREGISTER_CLASS`
across sessions), documented as if you have never seen this project.
Evidence labels: **[SOURCE]** = read directly from the header/impl,
**[EXPECTED]** = standard Godot behavior relied on but not locally
re-executed, **[INFERENCE]** = judgment/recommendation.

Classes are grouped by what they actually are in Godot's type system, not
by the loose "Node/Resource" framing alone, since several registered
classes are `RefCounted` subsystems rather than either:

- **Nodes** (5): things you place in a scene tree
- **Resources** (7): things you save as `.tres`/`.res` files
- **RefCounted subsystems** (11): non-node, non-resource classes the nodes/resources use internally, but which are still public and scriptable
- **Editor tools** (5): `TOOLS_ENABLED`-only, not present in exported games

---

# PART 1 — NODES

## MotionMatchingController (`Node`)

**Purpose [SOURCE]:** the orchestrator. Every other subsystem in this
addon exists to answer one question this class asks every tick: "given
where the character wants to go, which frame of which clip is the best
match?" It owns the trajectory predictor, the root motion integrator, the
search tree wrapper, the cache, the profiler, and (optionally) traversal
and motion-warp instances.

**What it does [SOURCE]:** each `_physics_process()` tick — updates the
trajectory from character state and desired velocity/facing; optionally
probes for a traversal obstacle; advances clip playback; polls an
in-flight async search result if one exists; runs a new search if due
(respecting `search_interval`); integrates root motion.

**Why it exists [INFERENCE]:** motion matching, as a technique, requires
one place that owns the search loop's timing and hysteresis (when to
search, when to accept a new match, how long to wait before switching
again) — scattering that logic across multiple nodes would make the
hysteresis rules impossible to reason about.

**When to use it [INFERENCE]:** as the animation driver for any humanoid
character (player or NPC) where you want data-driven, transition-free
locomotion instead of a hand-authored state machine.

**When NOT to use it [INFERENCE]:** for characters with only a handful of
clips and simple, discrete states (idle/walk/run/jump) where a normal
`AnimationTree` state machine is simpler to author and debug — motion
matching's value comes from *many* overlapping clips, not a few.

**Dependencies [SOURCE]:** `MotionMatchingResource` (assigned via the
`resource` property), an `AnimationTree` (bound automatically if found as a
sibling/ancestor — see `_bind_animation_tree()`), a `Node3D` character
(resolved via `character_path` or the parent node).

**Related classes [SOURCE]:** owns one each of `MMTrajectory`,
`MMRootMotion`, `MMPoseSearch`, `MMSearchCache`, `MMSearchWorker`,
`MMProfiler`; optionally references `MMTraversal`, `MMMotionWarp`.

**Typical workflow [SOURCE, from `demo/README.md`]:** build a database via
the editor panel → assign a `MotionMatchingResource` → add this node → set
`character_path` if the character isn't the parent → drive `set_desired_velocity()`/
`set_desired_facing()` from your input script every frame → read
`consume_root_motion()` each physics tick to move the character.

**Thread-safety [SOURCE]:** owns exactly one background thread
(`MMSearchWorker`), started/stopped by `rebuild()`/`set_resource()`
(the latter fixed this session to stop the worker *before* reassigning any
resource-owned `Ref`, closing a verified UAF race — see
`docs/PRE_RELEASE_REVIEW.md`). All public methods are expected to be called
from the main/game thread only **[EXPECTED]** — nothing about this class's
public API is documented or designed for concurrent external calls.

**Performance notes [SOURCE]:** `get_debug_info()` exposes
`search_time_usec`, `cache_hits`/`cache_misses`, and (as of this session)
`query_build_usec`/`continuation_eval_usec`/`switch_apply_usec`/
`update_total_usec`, plus the full `MMProfiler` report. `search_interval`
controls how often a real search runs; `0` means every tick.

**Serialization notes [SOURCE]:** the controller itself is not serialized
beyond its exported properties (`resource`, `character_path`, etc.) — all
the interesting state (database, schema, weights) lives in the
`MotionMatchingResource` it references.

---

## MMDebugDraw (`MeshInstance3D`)

**Purpose [SOURCE]:** visualizes the trajectory predictor and the matched
clip's own trajectory in the 3D viewport at runtime, using an
`ImmediateMesh`.

**What it does [SOURCE]:** past trajectory line, predicted future line, a
marker per sample point, and (optionally) the matched clip's trajectory
drawn on top so a mismatch between predicted and actual motion is visible.

**Why it exists [INFERENCE]:** motion-matching bugs are often about *why*
a particular clip was chosen, and that's much easier to diagnose visually
than by reading numbers in `get_debug_info()`.

**When to use it [INFERENCE]:** during development/tuning; strip it from
shipping builds (it's a `MeshInstance3D` doing per-frame immediate-mode
drawing, not something you want paying its cost in a shipped game)
**[INFERENCE]**.

**When NOT to use it:** in a release build, or on a character whose
trajectory you aren't actively debugging.

**Dependencies [SOURCE]:** a `MotionMatchingController` via `controller_path`.

**Related classes [SOURCE]:** reads `MMTrajectory`'s debug points/directions
through the controller.

**Typical workflow:** add as a sibling of the controller, set
`controller_path`, toggle `draw_trajectory`/`draw_matched_trajectory`/`draw_facing`.

**Thread-safety [EXPECTED]:** main-thread only, like all Godot rendering nodes.

**Performance notes [INFERENCE]:** immediate-mode mesh rebuilding every
`_process()` tick has a real cost; not free, hence "debug" in the name.

**Serialization notes [SOURCE]:** exported properties are just colors,
sizes, and toggles — no runtime state is persisted.

---

## MMFootIKModifier (`SkeletonModifier3D`)

**Purpose [SOURCE]:** ground adaptation for feet — stops feet floating on
stairs, sinking into slopes, and over-extending the standing leg, and locks
a planted foot in place for exactly as long as the source animation had it
planted.

**What it does [SOURCE]:** resolves 7 configured bone names (both legs +
pelvis) once, then each `_process_modification()` tick: raycasts under each
foot, analytically two-bone-solves each leg to the adjusted ground height,
adjusts the pelvis so the standing leg doesn't over-extend, and honors lock
state driven by `set_foot_contacts()` (fed from the database's baked
contact flags).

**Why it exists [INFERENCE]:** motion-captured or hand-authored locomotion
is only ever correct on the exact ground the animator built it for; any
other slope or stair immediately shows foot sliding/floating without this.

**When to use it [INFERENCE]:** any humanoid character that walks on
non-flat ground.

**When NOT to use it:** flying/swimming characters, or characters whose
feet never touch a walkable surface.

**Dependencies [SOURCE]:** a `Skeleton3D` (via `SkeletonModifier3D`'s own
mechanism), a physics world with raycastable ground geometry, and — for
foot locking specifically — a `MotionMatchingController` feeding
`set_foot_contacts()` from real per-frame contact data (this is the exact
data path that was broken and fixed in an earlier session — see
`AUDIT_REPORT.md`/`BUILD_READINESS_REPORT.md`).

**Related classes [SOURCE]:** uses `MMIKSolver::solve_two_bone()` internally.

**Typical workflow:** add under the character's `Skeleton3D`, set the 7
bone name properties to match your rig (defaults assume Mixamo-style
names), wire `set_foot_contacts()` from your character script each tick
using `MotionMatchingController::get_debug_info()`'s
`left_foot_contact`/`right_foot_contact` keys.

**Thread-safety [EXPECTED]:** main-thread only (physics raycasts,
skeleton pose writes).

**Performance notes [SOURCE]:** 2 raycasts/tick when both bone chains
resolve; analytic (non-iterative) leg solve, so cost is constant regardless
of how far off the ground the character is.

**Common mistakes [SOURCE, from the diagnostic added this session]:** bone
name properties not matching the actual skeleton — now surfaced via a
`WARN_PRINT_ONCE` at the first failed resolution instead of failing
silently.

---

## MMAimIKModifier (`SkeletonModifier3D`)

**Purpose [SOURCE]:** distributes an aim/look rotation across a bone chain
(e.g., spine → chest → neck → head) with per-bone weights and a cone
angle limit.

**What it does [SOURCE]:** each tick, smooths toward `target` (halflife
based) and rotates the configured chain toward it, respecting `max_angle`
so the character can't visually break its own neck.

**Why it exists [INFERENCE]:** aim/look-at is a near-universal need for
any character with a weapon, camera focus, or dialogue system, and doing
it well requires distributing the rotation rather than snapping one bone.

**When to use it [INFERENCE]:** aiming, look-at, camera-facing head
tracking.

**When NOT to use it:** anywhere a full IK chain to a *position* (not just
a direction) is needed — this is direction-only.

**Dependencies [SOURCE]:** a `Skeleton3D`; runs after `MMWarpModifier` by
design **[SOURCE, per its own doc comment]** so it aims from the
already-orientation-corrected pose.

**Related classes [SOURCE]:** conceptually paired with `MMWarpModifier`
(ordering matters, per the comment above) and `MMIKSolver::look_at_bone()`
(though this modifier's own implementation distributes across a *chain*,
not a single bone — worth confirming against source if extending it).

**Typical workflow:** add under the skeleton, set `chain_bones` and
`bone_weights` (one weight per bone in the chain), assign `target` from
gameplay code each tick.

**Thread-safety [EXPECTED]:** main-thread only.

---

## MMWarpModifier (`SkeletonModifier3D`)

**Purpose [SOURCE]:** pose-level warping applied *after* the animation has
already been sampled — orientation warping (running at an angle without a
dedicated diagonal clip), stride warping (step length scales with actual
speed, the single biggest cause of foot sliding), and procedural upper-body
lean in response to acceleration.

**Why it exists [SOURCE, from its own doc comment]:** these three
corrections are what let a small clip library cover a continuous range of
directions and speeds instead of needing a clip per direction/speed
combination.

**When to use it [INFERENCE]:** any locomotion character where you want
smooth, continuous direction/speed response rather than pure quantized clip
switching.

**Dependencies [SOURCE]:** `Skeleton3D`, resolves spine bone chain +
pelvis + both feet by name.

**Typical workflow:** add under the skeleton, set `pelvis_bone`,
`left_foot_bone`/`right_foot_bone`, `spine_bones`; drive
`orientation_angle`/`stride_scale`/`lean_amount` from gameplay each tick.

---

# PART 2 — RESOURCES

## MotionMatchingResource (`Resource`)

**Purpose [SOURCE]:** the single `.tres` that bundles everything a
controller needs, so swapping a character variant is one resource swap.

**Stored data [SOURCE]:** `animation_library`, `database`, `schema`,
`cost_function`, plus tuning properties (`search_interval`, `blend_time`,
`minimum_blend_time`, `switch_cooldown`, trajectory halflives, `max_speed`,
`quality`, `debug_enabled` — confirmed via `MM_ACCESSORS`/`MM_BIND_PROPERTY`
declarations in `motion_matching.hpp`).

**Serialization behavior [SOURCE]:** standard Godot `Resource`
serialization — every `ADD_PROPERTY`'d field round-trips through
`.tres`/`.res` automatically; no custom save/load code exists or is needed.

**Validation rules [SOURCE]:** `validate()` (added this session) checks
missing library (warning), missing/empty/zero-dimension database (errors),
schema/database dimension mismatch (error), `format_version` incompatibility
(warning), missing schema with no embedded fallback (warning). Returns an
`Array` of `{severity, clip, message}` dictionaries, `clip=""` for
resource-level issues.

**Relationships to other resources [SOURCE]:** references exactly one
`MotionMatchingDatabase`, one `MMFeatureSchema` (or falls back to the
database's embedded schema, or a default), one `MMCostFunction` (or
instantiates a default), and an `AnimationLibrary` (a core Godot type, not
one of ours).

**Runtime usage [SOURCE]:** read by `MotionMatchingController::_sync_from_resource()`
on `_ready()` and whenever `set_resource()` is called.

**Build-time usage [SOURCE]:** the editor's `MMDatabaseEditor` writes
`database`/`animation_library` into this resource as part of the
scan→build→save workflow.

**Typical creation workflow [SOURCE, from `demo/README.md`]:** create a new
`MotionMatchingResource`, assign an `AnimationLibrary`, use the editor
panel to scan the skeleton, build the database, save it, assign the saved
database back into this resource.

---

## MotionMatchingDatabase (`Resource`)

**Purpose [SOURCE]:** the built, searchable frame data — one row per
sampled frame, flat packed arrays for cache-friendly search.

**Stored data [SOURCE]:** per-frame animation id, time, normalized time,
root velocity, angular velocity, contact bitmask, plus the full flat
feature-vector block (`_features`), dimension, sample rate, and (as of this
session) `format_version`.

**Serialization behavior [SOURCE]:** uses native `Resource` properties via
`MM_BIND_STORAGE` for the large arrays — no custom binary format, which
prior sessions' audits confirmed as one of the codebase's solid points.

**Validation rules [SOURCE]:** `is_format_compatible()` (added this
session) compares stored `format_version` against
`MM_DATABASE_FORMAT_VERSION` (currently `1`); a database saved before this
field existed reads as `0`, distinguishable from a real mismatch.

**Relationships to other resources [SOURCE]:** built by
`MMFeatureExtractor::build_database()`; consumed by `MMPoseSearch::build()`,
`MMRootMotion`, `MotionMatchingController`.

**Runtime usage [SOURCE]:** read-only once built — `rebuild()` on the
controller calls `_search->build(_database)`.

**Build-time usage [SOURCE]:** written incrementally by
`MMFeatureExtractor` (per-clip `append_animation()` calls), then
`finalize(dimension)` stamps `format_version` and locks in the frame count.

---

## MMAnimationEntry (`Resource`)

**Purpose [SOURCE]:** one row of per-clip metadata inside a database —
name, library name (for qualified naming), tags, category, length.

**Stored data [SOURCE]:** `animation_name`, `library_name`, `tags`
(bitmask), `category`, plus `get_qualified_name()` (a computed
`library/name` string, not stored).

**Relationships [SOURCE]:** one instance per clip inside a
`MotionMatchingDatabase`; referenced by frame-index-to-clip lookups
throughout the search/playback code.

---

## MMFeatureSchema (`Resource`)

**Purpose [SOURCE]:** describes what a feature vector contains and where
each value lives inside it — the single source of truth for feature
layout, read by the database, extractor, cost function, and runtime query
alike.

**Stored data [SOURCE]:** trajectory sample times, tracked pose bones, root
bone name, an optional `MMSkeletonProfile`, toggles for including hands/bone
velocity/root velocity, extra user dimensions — plus a cached, rebuilt-on-change
layout (offsets per feature group, total dimension).

**Why it exists [SOURCE, from its own doc comment]:** so adding a new
feature never requires touching search code — every consumer reads layout
from this one resource. Feature order is a deliberate prefix structure so a
lower LOD search is the same loop with a smaller bound.

**Validation rules [SOURCE]:** none of its own beyond what
`MotionMatchingResource::validate()` checks (dimension consistency against
the database it's paired with).

**Serialization notes [SOURCE]:** `_dimension` and all offset fields are
"cached, rebuilt whenever a setting changes" per its own comment — i.e.,
derived state, not independently authored; changing e.g. `pose_bones` after
load triggers `_rebuild_layout()`.

**Typical creation workflow [SOURCE]:** `apply_skeleton_profile(skeleton)`
runs detection and fills the pose bone list automatically — this is the
*only* place bone names enter the framework, per its own doc comment.

---

## MMSkeletonProfile (`Resource`)

**Purpose [SOURCE]:** universal rig detection — maps semantic bone *roles*
(22 of them, `MMBoneRole` enum) to whatever names a specific skeleton
actually uses, so the rest of the framework never needs to know or guess
bone names.

**Internal workflow [SOURCE]:** three passes, strongest evidence first —
(1) structural graph analysis (leg chains, head, arms identified from
skeleton topology alone, works even on `bone_001`-style anonymous names),
(2) normalized token-based name matching (disambiguates left/right,
recognizes Mixamo/UE/Rokoko/Blender/mocap conventions), (3) manual
overrides, which always win and persist across re-detection.

**Why it exists [SOURCE]:** this is the single mechanism that makes the
whole addon animation-pack-agnostic — confirmed as a deliberate,
consolidated design (see the "Universal Rig Detection" section of
`docs/HANDOFF_PACKAGE.md`, which traces the earlier consolidation from two
competing systems down to this one).

**Validation rules [SOURCE]:** `is_detected()`, `has_role()`,
`get_missing_roles()`, `get_detection_report()` — a human-readable summary
string is stored (`_detection_report`), not just a boolean pass/fail.

**Typical creation workflow:** `auto_detect(skeleton)`, inspect
`get_missing_roles()`, manually assign any gaps via `set_bone_name()`.

---

## MMClipAnalyzer (`Resource`)

**Purpose [SOURCE]:** classifies clips by *measured motion*
(speed/turn-rate/airtime/contact-ratio), not by file name — an untitled
mocap take and a clip named `LOC_RUN_FWD_01` classify identically if they
move identically.

**Stored data [SOURCE]:** speed-band thresholds (idle/walk/jog/run), motion
shape thresholds (turn, strafe, backward, crouch, airborne, traversal
height, start/stop delta), an optional name-rule dictionary that ships
**empty by default** (an override for teams with a strict naming
convention, never a requirement, per its own doc comment).

**Important algorithm [SOURCE]:** `calibrate_speed_bands()` derives speed
bands from the *actual* distribution of a specific library's clips
(quartiles of moving clips, idle excluded), so defaults adapt to a human
rig, a quadruped, or something stylized without manual retuning.

**Relationships [SOURCE]:** consumed by `MMFeatureExtractor` during
database building; `category_for_tags()` is also called statically by
`MMFeatureExtractor::guess_category_from_tags()`.

---

## AnimationNodeMotionMatching (`AnimationRootNode`, i.e. a `Resource`)

**Purpose [SOURCE]:** the `AnimationTree` integration point — drop this in
as the tree root (or inside a `BlendTree`) and the tree plays whatever the
bound `MotionMatchingController` decided that tick.

**How it works internally [SOURCE]:** blends exactly two clips (the one
being left, the one being entered) using `blend_animation()` with an
*absolute* time — meaning a match can start at, say, 4.7 seconds into a
clip instead of at zero. This is, per its own doc comment, "the whole point
of motion matching and the thing a state machine cannot do."

**Required setup [SOURCE]:** `controller_path` pointing at a
`MotionMatchingController` (or the controller calls `bind_controller()`
itself in its own `_ready()` so this node never has to search the tree
during animation processing).

**Serialization notes [SOURCE]:** as an `AnimationNode`/`Resource`, this is
saved as part of the `AnimationTree`'s tree structure, not independently.

---

# PART 3 — REFCOUNTED SUBSYSTEMS

## MMPoseSampler (`RefCounted`)

**Purpose [SOURCE]:** resolves an `Animation` against a `Skeleton3D` once
in `bind()`, then samples cheaply — track lookup by bone name happens a
single time, after which sampling is an array index, not a string compare.

**Why it exists [SOURCE]:** the feature extractor needs to sample
thousands of frames across potentially hundreds of clips; doing a string
compare per bone per frame would be prohibitively slow.

**Notable detail [SOURCE]:** track paths are matched on their last
subname (the bone), so a clip authored against
`"Armature/Skeleton3D:Hips"` and one authored against
`"%GeneralSkeleton:Hips"` both bind without path rewriting. When a clip has
no root track, the pelvis is projected onto the ground plane and used
instead — this is what makes in-place animation packs work without manual
conversion.

---

## MMFeatureExtractor (`RefCounted`)

**Purpose [SOURCE]:** converts any `AnimationLibrary` into a
`MotionMatchingDatabase`. Nothing pack-specific is encoded here — rig
comes from `MMSkeletonProfile`, classification from `MMClipAnalyzer`,
layout from `MMFeatureSchema`.

**Typical workflow [SOURCE]:** `analyze_library()` (automatic tagging
pass, meant to be *reviewed*, not blindly trusted, per its own comment) →
`build_database()` (main entry point, clip settings optional — anything
missing is filled in by analysis).

**Related static utilities [SOURCE]:** `guess_tags_from_name()`/
`guess_category_from_tags()` are name-only convenience wrappers for before
a skeleton is available (the editor's Scan step runs before Build) —
explicitly documented as delegating to the same canonical `MMClipAnalyzer`,
not a second classification system.

---

## MMAnimationLibraryTools (`RefCounted`)

**Purpose [SOURCE]:** a small set of static utility functions —
`auto_tag_library()` (bulk-suggest tags/categories), `validate_library()`
(pre-flight checks: no root track, missing tracked bones, zero-length
clips — catches problems *before* hours are spent building a database),
`merge_libraries()` (flattens several libraries into one, e.g. combining
unarmed/rifle/traversal sets).

---

## MMCostFunction (`RefCounted`)

**Purpose [SOURCE]:** turns two normalized feature vectors into a single
scalar cost. Per-feature-group weights are baked into a per-dimension
weight table so the inner loop is branchless weighted squared distance.

**Important algorithm [SOURCE]:** `compute_raw()` takes an early-out bound
— once accumulated cost exceeds it, the function returns `MM_INFINITY`
immediately, which "typically skips 60 to 80 percent of the work" per its
own comment. The same weight table is handed to the KD-tree, which needs it
for correct pruning.

**Extensibility [SOURCE]:** `compute_cost()` is `virtual` — inheritable in
GDScript or C++ to override the metric, at the cost of the fast path (the
scriptable entry point is explicitly documented as "slower, meant for
prototyping").

**Relationships [SOURCE]:** `switch_penalty` exists as a property (extra
cost for any frame that isn't a continuation of the current clip) but —
per the previous audit's findings — was not confirmed to actually be read
anywhere in `_should_switch()`; this remains an open, unconfirmed item, not
re-verified this session.

---

## MMPoseSearch (`RefCounted`)

**Purpose [SOURCE]:** the KD-tree acceleration structure over a built
database, plus a brute-force reference implementation
(`search_brute_force()`, and — added this session —
`search_brute_force_query()`, a GDScript-callable wrapper used by
`test_kdtree_vs_bruteforce.gd`).

**Important algorithm [SOURCE]:** splits on the widest axis (estimated
from a bounded sample) rather than cycling dimensions, keeping build time
roughly linear even for large databases.

**Performance notes [SOURCE]:** `search_query()` is the scriptable, slower
entry point (Dictionary in/out) for tools/debugging; the raw pointer
`search()` is the hot path the controller actually calls.

---

## MMSearchCache (not a `GDCLASS` — a plain internal C++ class)

**Purpose [SOURCE]:** direct-mapped cache keyed on a quantized query +
filter, avoiding a full tree descent for a repeated or near-repeated query
under the same gameplay-state filter.

**Note on scope [SOURCE]:** unlike every other class in this document,
`MMSearchCache` is **not** registered with Godot (no `GDCLASS`) — it's a
private implementation detail of `MotionMatchingController`, exposed only
indirectly through `get_debug_info()`'s `cache_hits`/`cache_misses` keys.
Included here for completeness since it's a "major system" per Section 4
of this review, not because it's part of the public class surface.

**Thread-safety [SOURCE]:** not thread-safe itself (no internal locking);
correctness relies entirely on only ever being touched from the main
thread inside `_run_search()`/`_consume_result()` — confirmed by source
inspection this session.

---

## MMSearchWorker (not a `GDCLASS` — declared alongside `MMSearchCache` in `cache.hpp`)

**Purpose [SOURCE]:** one background thread, single-slot
request/response — the controller submits a query and reads back the most
recent result on a later frame; a deeper queue would only add latency.

**Thread-safety [SOURCE]:** genuinely reviewed this session — `submit()`/
`poll()` both lock a `std::mutex` around the shared slots; the wait
predicate in `_thread_main()` correctly wakes on either a new request or
shutdown, so `stop()` cannot deadlock. The database and search tree are
read without a lock because they're immutable once built (protected by
`rebuild()`'s `stop()`-before-`build()` ordering, not by a mutex). A raw
`MMCostFunction*` is temporarily promoted to a `Ref` during each search
specifically to survive a concurrent main-thread `Ref` drop.

---

## MMProfiler (`RefCounted`)

**Purpose [SOURCE]:** search-time percentiles, switch-rate, and budget
overrun counting, in one `Dictionary` (`get_report()`) meant — per its own
doc comment — to be dropped directly into `get_debug_info()`, which this
session's integration finally did.

**Data flow [SOURCE]:** fed via `record()` (search stats) and
`record_switch()` (clip switches), both called from the controller; never
fed by cache hits (deliberately, to avoid diluting the percentiles with
near-zero-cost samples).

---

## MMTrajectory (`RefCounted`)

**Purpose [SOURCE]:** predicts where the character *wants* to be, not
where it currently is, using a critically-damped spring integrated in
fixed sub-steps — never overshoots, reacts instantly to intent changes,
decays toward a stop within one halflife.

**Important algorithm [SOURCE]:** `_spring_step()` is the core integrator.
Everything the search consumes is produced in *character space*, so a
query built while facing north matches a clip authored facing any
direction.

**Notable feature [SOURCE]:** `set_obstacle()`/`clear_obstacle()` truncate
the predicted line at a wall instead of letting it push through geometry;
`set_external_samples()` lets AI/networked replay override the prediction
entirely.

---

## MMRootMotion (`RefCounted`)

**Purpose [SOURCE]:** integrates root displacement from *velocity*, never
from the difference between two absolute root transforms — this single
decision is what makes a frame jump safe, since two arbitrary clips' frame
transforms are unrelated but their velocities are directly comparable.

**Verified defect fixed this session [SOURCE]:** `notify_frame_jump()`
wrote to `_blend_linear`/`_blend_angular` but `update()` never read them —
a jump was silently still eased in over `blend_halflife` instead of
adopted immediately, contradicting the function's own comment. Fixed via a
`_jump_pending` flag.

**Notable feature [SOURCE]:** `report_position_error()`/`_correction_strength`
let physics-vs-animation drift be fed back and corrected gradually rather
than snapping.

---

## MMTraversal (`RefCounted`)

**Purpose [SOURCE]:** raycasts along the predicted trajectory to classify
an approaching obstacle (vault, mantle, climb, slide, roll — per
`docs/architecture.md`).

**Integration status [SOURCE, this session]:** wired into
`MotionMatchingController` as of this session — opt-in via
`set_traversal()`, probed once per grounded tick via
`_evaluate_traversal()`, emits the previously-dead `traversal_requested`
signal, biases `_build_filter()`'s category/tags via
`get_required_tags()`. **Never executed** — logically wired, not
runtime-confirmed.

---

## MMMotionWarp (`RefCounted`)

**Purpose [SOURCE]:** bends a clip's root motion toward a target it
wasn't authored for (e.g., one vault animation applied to a thousand
different obstacle placements), applied to the root *delta* so the pose
itself is never distorted.

**Integration status [SOURCE, this session]:** wired into the controller
via `set_motion_warp()`/`begin_warp()`/`end_warp()`/`is_warping()`;
`consume_root_motion()` routes through `warp_delta()` only when active.
**Known unsolved edge case [SOURCE]:** no automatic handling if a clip
switch happens mid-warp — explicitly left as caller responsibility.

---

## MMIKSolver (`RefCounted`)

**Purpose [SOURCE]:** stateless solver library shared by the IK modifiers
and traversal — analytic two-bone (exact, one iteration, "the only one that
should touch legs" per its own comment), FABRIK and CCD (iterative, for
longer chains), `look_at_bone()` (cone-limited direction solve).

---

# PART 4 — EDITOR TOOLS (`TOOLS_ENABLED` only)

## MMDatabaseEditor (`VBoxContainer`)
**[SOURCE]** Build/inspect/save a motion database; the clip table is the
important part — "with 800 clips, tagging is the job."

## MMFeatureEditor (`VBoxContainer`)
**[SOURCE]** Schema layout and cost weights, with sliders normalized to a
percentage of total cost.

## MMTrajectoryEditor (`VBoxContainer`)
**[SOURCE]** Tunes the predictor's halflives — "the parameter set most
likely to be tuned by someone who did not write the code."

## MMDebugTools (`VBoxContainer`)
**[SOURCE]** Live profiler readout for the selected controller while the
game runs (in the editor, via remote debugging).

## MotionMatchingEditorPlugin (`EditorPlugin`)
**[SOURCE]** Adds one bottom panel holding every tool above, "so the whole
workflow lives in one place instead of across four inspectors."
