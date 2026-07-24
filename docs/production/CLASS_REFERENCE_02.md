# MOTION MATCHING — Production Class Reference (Part 2 of 3)

Continued from Part 1. Same evidence labeling convention.

---

# MotionMatchingDatabase (`Resource`)

**1. Purpose [SOURCE]:** the built, searchable frame data underlying every
search — one logical row per sampled animation frame, stored as flat
packed arrays for cache-friendly, allocation-free search.

**2. Overview:** holds per-frame animation id, playback time, normalized
time, root velocity, angular velocity, a 2-bit foot-contact bitmask, and
the full flat feature-vector block, plus dimension, sample rate, and (as
of this addon's current state) a `format_version` stamp.

**3. Internal Workflow:** built incrementally by `MMFeatureExtractor`
(one `append_animation()` call per clip), then locked via
`finalize(dimension)`, which stamps `format_version` and fixes the frame
count. From that point it is read-only.

**4. Runtime Behavior:** read by `MMPoseSearch::build()` (once, at
controller `rebuild()` time) and by per-frame accessors during search
result reconstruction (`get_frame_animation_id`/`get_frame_time_value`/
`get_frame_normalized_value`/`get_frame_root_velocity_value`/
`get_frame_angular_value`/`get_frame_contacts_value`). Never mutated at
runtime.

**5. When to Use:** always — this is the data every controller needs;
there is no lighter-weight alternative within this addon.

**6. When NOT to Use:** n/a — this is not an optional component.

**7. Inputs:** produced by `MMFeatureExtractor`; not hand-authored.

**8. Outputs:** per-frame scalar/vector accessors (see above); `Array`
list of `MMAnimationEntry` (one per clip); `is_format_compatible()` bool.

**9. Properties**
| Name | Type | Purpose | Serialization |
|---|---|---|---|
| `sample_rate` | `float` | Frames-per-second the database was sampled at | Saved |
| `format_version` | `int` | Stamped by `finalize()`; `0` means pre-versioning | Saved |
| (large packed arrays: features, animation ids, times, velocities, contacts) | `PackedFloat32Array`/`PackedInt32Array`/etc. | The actual searchable data | Saved via `MM_BIND_STORAGE` (native `Resource` properties, no custom binary format) |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `finalize(dimension)` | Locks the database, stamps `format_version` | — |
| `get_frame_count()` / `get_dimension()` | Basic metadata | `int` |
| `is_format_compatible()` | Compares stamped version against the addon's current constant | `bool` |
| `get_frame_animation_id/time_value/normalized_value/root_velocity_value/angular_value/contacts_value(frame)` | Per-frame scalar/vector reads | various |
| `get_animation_entry(id)` | Clip metadata for a given id | `MMAnimationEntry` |
| `normalize_query(raw_vector)` | Applies the same per-group normalization used when the database was built, to an arbitrary query vector | `PackedFloat32Array` |

**11. Signals:** none.

**12. Dependencies:** built by `MMFeatureExtractor`; consumed by
`MMPoseSearch`, `MMRootMotion`, `MotionMatchingController`; described by
`MMFeatureSchema`.

**13. Data Flow:** `AnimationLibrary` + `Skeleton3D` → extractor → this
database → `finalize()` → saved `.tres` → loaded at runtime → read-only
thereafter.

**14. Execution Order:** build-time only; no per-tick behavior of its own.

**15. Performance Notes:** flat packed-array layout is deliberately
cache-friendly for the search's sequential/near-sequential access
pattern **[INFERENCE, standard rationale for this data layout]**.

**16. Thread Safety:** safe to read concurrently once built and
`finalize()`d, since nothing mutates it afterward — this is exactly what
lets `MMSearchWorker` read it from a background thread without a lock.

**17. Serialization:** `.tres`/`.res`, native `Resource` properties.

**18. Limitations:** not incrementally updatable at runtime — adding a
clip means rebuilding the whole database offline/in the editor.

**19. Best Practices:** always check `is_format_compatible()` (or use
`MotionMatchingResource::validate()`, which does this for you) after
loading a database that might have been built by a different addon
version.

**20. Example:**
```gdscript
if not database.is_format_compatible():
    push_warning("Database format_version %d does not match this addon build" % database.format_version)
```

**21. Related Classes:** `MMFeatureExtractor` (builder),
`MMAnimationEntry` (per-clip metadata rows), `MMPoseSearch` (its
consumer), `MMFeatureSchema` (describes its layout).

---

# MMFeatureSchema (`Resource`)

**1. Purpose [SOURCE]:** the single description of what a feature vector
contains and where each value lives inside it, so every consumer (database,
extractor, cost function, runtime query) reads layout from one place —
adding a feature never requires touching search code.

**2. Overview:** describes a feature vector as a concatenation of groups —
trajectory positions, trajectory directions, root velocity (optional),
tracked bone positions, tracked bone velocities (optional), extra
user-defined dimensions (optional) — in that deliberate order, so every
quality/LOD level is a prefix of the full vector.

**3. Internal Workflow:** `_rebuild_layout()` recomputes offsets and total
dimension whenever a relevant setting changes (trajectory times, pose
bones, toggles, extra dimension count).

**4. Runtime Behavior:** read-only at runtime by the extractor (build
time) and by the cost function/controller (to interpret query/frame
layout). Not itself tick-driven.

**5. When to Use:** one schema per distinct feature layout you want —
typically one per game/character archetype, shared across many databases
if they use the same layout.

**6. When NOT to Use:** don't create a new schema per controller instance
if they share the same layout — schemas are meant to be shared assets.

**7. Inputs:** `apply_skeleton_profile(skeleton)` — the *only* place bone
names enter the framework, per its own doc comment; runs detection and
fills the pose bone list from detected roles.

**8. Outputs:** `get_dimension()`, per-group offsets, `make_vector()`
(zeroed vector matching the schema), `get_lod_dimension(quality)`.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `trajectory_times` | `PackedFloat32Array` | project-configured | Sample times for the trajectory feature block |
| `pose_bones` | `PackedStringArray` | filled by `apply_skeleton_profile()` | Which bones' positions/velocities are tracked |
| `root_bone` | `String` | filled by detection | Root bone name |
| `skeleton_profile` | `MMSkeletonProfile` | null | Optional linked profile |
| `include_hands` | `bool` | false | Whether hand bones are included in the default pose bone set |
| `include_bone_velocity` | `bool` | true | Include per-bone velocity block **[naming inconsistency — see `PRE_RELEASE_REVIEW.md`: this boolean getter doesn't follow the codebase's `is_X` convention]** |
| `include_root_velocity` | `bool` | true | Include root velocity block (same naming note applies) |
| `extra_dimensions` | `int` | 0 | User-defined extra feature slots |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `apply_skeleton_profile(skeleton)` | Runs detection, fills pose bones | `bool` |
| `get_dimension()` / `get_*_offset()` | Layout queries | `int` |
| `get_group_of_dimension(i)` | Which feature group a given dimension belongs to (for weighting) | `int` |
| `make_vector()` | Zeroed vector matching this schema | `PackedFloat32Array` |
| `get_lod_dimension(quality)` | Reduced dimension bound for a quality level | `int` |
| `make_default()` (static) | A reasonable default schema | `Ref<MMFeatureSchema>` |

**11. Signals:** none.

**12. Dependencies:** references `MMSkeletonProfile` (optional). Depended
on by `MMFeatureExtractor`, `MMCostFunction`, `MotionMatchingDatabase`,
`MotionMatchingResource`.

**13. Data Flow:** skeleton → `apply_skeleton_profile()` → layout →
consumed by every downstream system needing to know "what is dimension N."

**14. Execution Order:** configured before a database build; read
thereafter.

**15. Performance Notes:** `_rebuild_layout()` is cheap (metadata-only,
not per-frame data) and only runs on setting changes.

**16. Thread Safety:** no threading concerns; main-thread/editor-time use.

**17. Serialization:** `.tres`/`.res`, standard `Resource`.

**18. Limitations:** changing the schema after a database is built
requires rebuilding the database — `MotionMatchingResource::validate()`
is what catches a stale pairing.

**19. Best Practices:** call `apply_skeleton_profile()` once per rig, not
per clip — bone names should never be assumed or hardcoded anywhere else.

**20. Example:**
```gdscript
var schema := MMFeatureSchema.new()
if not schema.apply_skeleton_profile(my_skeleton):
    push_error("Could not resolve pose bones from this skeleton")
```

**21. Related Classes:** `MMSkeletonProfile` (rig detection it consumes),
`MMFeatureExtractor` (its consumer at build time), `MMCostFunction`
(reads its group layout for weighting).

---

# MMCostFunction (`RefCounted`)

**1. Purpose [SOURCE]:** turns two normalized feature vectors into a
single scalar cost — the distance metric the search optimizes.

**2. Overview:** per-feature-group weights (`weight_trajectory_position`,
`weight_trajectory_direction`, `weight_pose_position`,
`weight_pose_velocity`, `weight_root_velocity`, `weight_extra`,
`switch_penalty`) are baked into a flat per-dimension weight table via
`rebuild(schema)`.

**3. Internal Workflow:** `compute_raw()` (the hot path) is a branchless
weighted squared distance loop with an early-out bound — once accumulated
cost exceeds the bound, it returns `MM_INFINITY` immediately, skipping the
remaining dimensions.

**4. Runtime Behavior:** called once per candidate frame during a KD-tree
descent (via the tree's pruning logic) and once per accepted comparison
during a brute-force search.

**5. When to Use:** the default cost function is usually sufficient;
override `compute_cost()` (the virtual, scriptable entry point) only when
prototyping a genuinely different metric — it is explicitly documented as
slower than the fast path.

**6. When NOT to Use:** don't call the scriptable `compute_cost()` from
inside a tight per-frame loop in your own code — use `compute_raw()` (C++)
or accept that GDScript-level cost computation is for tooling/prototyping,
not the hot path.

**7. Inputs:** `schema` (for weight-table layout), two feature vectors
(query, frame) at call time.

**8. Outputs:** a single `float` cost; `compute_group_errors()` — a
per-group breakdown `PackedFloat32Array` for debug panels.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `weight_trajectory_position` | `float` | 1.0 | Trajectory position group weight |
| `weight_trajectory_direction` | `float` | 1.0 | Trajectory direction group weight |
| `weight_pose_position` | `float` | 0.75 | Bone position group weight |
| `weight_pose_velocity` | `float` | 0.75 | Bone velocity group weight |
| `weight_root_velocity` | `float` | 1.0 | Root velocity group weight |
| `weight_extra` | `float` | 1.0 | Extra/user dimension weight |
| `switch_penalty` | `float` | 0.0 | Extra cost for a non-continuation frame — **[SOURCE, prior-session finding, not re-verified this pass]** not confirmed to actually be consulted anywhere in the controller's switch-decision logic; treat as unconfirmed rather than assume it works |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `rebuild(schema)` | Recomputes the per-dimension weight table | — |
| `is_dirty()` / `mark_dirty()` | Whether a rebuild is pending | `bool` / — |
| `compute_cost(query, frame)` | Scriptable, slower entry point; `virtual`, overridable | `float` |
| `compute_group_errors(query, frame)` | Per-group cost breakdown | `PackedFloat32Array` |
| `set_group_weight(group, w)` / `get_group_weight(group)` | Weight by group enum instead of by name | — / `float` |

**11. Signals:** none.

**12. Dependencies:** requires an `MMFeatureSchema` (via `rebuild()`).
Used by `MMPoseSearch` (pruning) and `MotionMatchingController`
(query-vs-frame comparisons).

**13. Data Flow:** schema → weight table → per-comparison scalar cost.

**14. Execution Order:** `rebuild()` called whenever the schema changes;
`compute_raw()` called per candidate during every real search.

**15. Performance Notes:** the early-out bound in `compute_raw()`
typically skips 60-80% of dimension comparisons per its own doc comment
**[SOURCE, a stated design rationale, not a benchmark]**.

**16. Thread Safety:** stateless per-call aside from the weight table
(read-only during search) — safe to call from the background search
thread, which is exactly what `MMSearchWorker` does via a temporarily
Ref-promoted raw pointer.

**17. Serialization:** `.tres`/`.res` if saved standalone, or embedded in
a `MotionMatchingResource`.

**18. Limitations:** `switch_penalty`'s actual wiring is unconfirmed (see
Properties).

**19. Best Practices:** call `rebuild()` after any schema change, not
just once at startup, or the weight table will silently describe a stale
layout.

**20. Example:**
```gdscript
var cost := MMCostFunction.new()
cost.weight_pose_velocity = 1.0  # Emphasize matching current limb speed
cost.rebuild(schema)
```

**21. Related Classes:** `MMFeatureSchema` (its layout source),
`MMPoseSearch` (its consumer for tree pruning).

---

# MMPoseSearch (`RefCounted`)

**1. Purpose [SOURCE]:** the KD-tree acceleration structure over a built
database, plus a brute-force reference implementation for correctness
testing.

**2. Overview:** `build(database)` constructs the tree once; `search()`
descends it for the best match under a filter and cost function;
`search_brute_force()`/`search_brute_force_query()` (the latter added this
session, GDScript-callable) scan every frame linearly as a correctness
oracle.

**3. Internal Workflow:** `_build_recursive()` splits on the widest axis
(estimated from a bounded sample, not the full dataset), keeping build
time roughly linear even for large databases.

**4. Runtime Behavior:** `search()` is called once per real (non-cached)
search, either synchronously on the main thread or from
`MMSearchWorker`'s background thread.

**5. When to Use:** always, as the acceleration structure under a
controller — there's no reason to bypass it for real gameplay use.

**6. When NOT to Use:** don't use `search_brute_force()` in gameplay code
— it's a linear scan, meant for correctness verification only (see
`test_kdtree_vs_bruteforce.gd`).

**7. Inputs:** a `MotionMatchingDatabase` (for `build()`); a query vector,
`MMCostFunction`, `MMSearchFilter`, `MMSearchContext` (for `search()`).

**8. Outputs:** `MMMatchResult` (frame index, animation id, time,
normalized time, cost) + `MMSearchStats` (frames compared, candidates
visited, nodes visited, time, cache hit/miss counts — the latter two only
meaningful when read through the controller, which owns the cache).

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `leaf_size` | `int` | project-configured | Tree leaf size; affects the build/tree-shape tradeoff |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `build(database)` | Constructs the KD-tree | `bool` |
| `is_built()` | Whether a tree currently exists | `bool` |
| `clear()` | Discards the tree | — |
| `search_query(query, cost, required_tags, blocked_tags, category_mask)` | Scriptable tree search | `Dictionary` |
| `search_brute_force_query(...)` (added this session) | Scriptable linear-scan reference search, same signature shape as `search_query` | `Dictionary` |
| `get_node_count()` | Tree size metric | `int` |

**11. Signals:** none.

**12. Dependencies:** requires a built `MotionMatchingDatabase` and an
`MMCostFunction`. Used directly by `MotionMatchingController` and
`MMSearchWorker`.

**13. Data Flow:** database → `build()` → tree → `search()` (per query) →
match result.

**14. Execution Order:** `build()` once per `rebuild()` call on the
controller; `search()` potentially every controller tick (subject to
`search_interval`).

**15. Performance Notes:** KD-tree descent is expected to be
substantially faster than the brute-force linear scan on any non-trivial
database — this is the entire reason the tree exists — but no comparative
timing has been measured in this environment; `test_kdtree_vs_bruteforce.gd`
checks *correctness* (agreement), not speed.

**16. Thread Safety:** read-only after `build()`, so safe to call
`search()` from a background thread without a lock, provided no
concurrent `build()`/`clear()` is happening — which the controller
guarantees by calling `_worker.stop()` (a full thread join) before ever
rebuilding the tree.

**17. Serialization:** not itself serialized — rebuilt from the database
every time a controller starts.

**18. Limitations:** rebuilding is not incremental; the whole tree is
reconstructed from scratch on every `build()` call.

**19. Best Practices:** don't call `build()` more often than necessary —
it's meant to run once per database (re)assignment, not per frame.

**20. Example:**
```gdscript
var search := MMPoseSearch.new()
search.build(database)
var result: Dictionary = search.search_query(query, cost, 0, 0, -1)
```

**21. Related Classes:** `MMSearchCache` (checked before this class is
even consulted), `MMSearchWorker` (runs this class's `search()` on a
background thread), `MMCostFunction` (pruning metric).

---

# MMTrajectory (`RefCounted`)

**1. Purpose [SOURCE]:** predicts where the character *wants* to be, not
where it currently is, so the search query describes intent rather than
history alone.

**2. Overview:** a critically-damped spring integrated in fixed
sub-steps — never overshoots, reacts instantly to a change of intent,
decays a released input toward a stop within one halflife.

**3. Internal Workflow:** `update(delta)` integrates `_spring_step()`
forward for both position and direction, producing a set of future sample
points at configured lookahead times; a rolling history buffer of past
positions is also maintained.

**4. Runtime Behavior:** updated once per controller tick, before the
query is built — "the search is only as good as the intent it is given"
per the controller's own internal comment.

**5. When to Use:** always, as the standard intent predictor under a
controller.

**6. When NOT to Use:** if you have externally-computed trajectory data
(e.g., from an AI path or a networked replay), use
`set_external_samples()` instead of feeding raw velocity/facing — this
overrides the internal prediction entirely rather than fighting it.

**7. Inputs:** current position/velocity/facing, desired velocity/facing
(from the controller, ultimately from gameplay); optionally an obstacle
distance/normal to truncate the predicted line.

**8. Outputs:** future sample positions/directions/velocities (read by
`write_features()` to fill the query's trajectory block); debug points
for visualization.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `halflife_position` | `float` | 0.12 | How quickly predicted position decays toward the goal |
| `halflife_direction` | `float` | 0.10 | Same, for facing direction |
| `prediction_step` | `float` | 1/60 | Sub-step size for the spring integration |
| `history_duration` | `float` | 0.6 | How much past trajectory is retained |
| `history_interval` | `float` | 0.1 | Sampling interval for the history buffer |
| `max_speed` | `float` | 6.0 | Clamp on predicted speed |

**10. Methods**
| Method | Description |
|---|---|
| `configure(schema)` | Sets up sample times from the schema |
| `set_state(pos, vel, facing)` | Feeds current character state |
| `set_desired_velocity(v)` / `set_desired_facing(v)` | Feeds intent |
| `set_obstacle(distance, normal)` / `clear_obstacle()` | Wall-awareness truncation |
| `update(delta)` | Integrates the spring, rebuilds future samples |
| `set_external_samples(positions, directions)` | Overrides prediction entirely |
| `write_features(query, schema, character_basis)` | Fills the trajectory block of a query vector, in character space |

**11. Signals:** none.

**12. Dependencies:** requires an `MMFeatureSchema` (for sample-time
configuration). Owned by `MotionMatchingController`; feeds
`MMTraversal::probe()` and the query-building step.

**13. Data Flow:** character state + intent → spring integration →
future/history samples → character-space query features.

**14. Execution Order:** updated once per controller tick, before
traversal probing and query building.

**15. Performance Notes:** fixed sub-step integration means cost scales
with `prediction_step` and the number of future sample points, both
small, bounded numbers — not database-size-dependent.

**16. Thread Safety:** main-thread only — updated as part of the
controller's synchronous per-tick update.

**17. Serialization:** not itself a `Resource`; its tuning properties are
typically configured once (often from `MotionMatchingResource`'s
trajectory halflife properties) rather than saved independently.

**18. Limitations:** the spring model assumes reasonably smooth intent
changes — extremely abrupt, discontinuous desired-velocity inputs every
single tick could produce a jittery prediction **[INFERENCE, general
property of spring-based predictors, not a specific bug found in this
codebase]**.

**19. Best Practices:** feed `set_desired_velocity()`/`set_desired_facing()`
every tick, even when unchanged — the spring needs a consistent signal to
predict against.

**20. Example:**
```gdscript
# Typically not touched directly — the controller owns and updates its
# own MMTrajectory instance every tick. Direct use is mainly for reading
# debug data:
var points: PackedVector3Array = controller.get_debug_trajectory()
```

**21. Related Classes:** `MMFeatureSchema` (sample-time configuration),
`MMTraversal` (consumes trajectory for obstacle probing),
`MotionMatchingController` (owner and driver).

---

# MMRootMotion (`RefCounted`)

**1. Purpose [SOURCE]:** integrates character displacement from
*velocity*, never from the difference between two absolute root
transforms — the specific design decision that makes a motion-matching
frame jump safe, since two unrelated clips' absolute transforms don't
compare meaningfully, but their velocities do.

**2. Overview:** reads baked per-frame root/angular velocity from the
database, smooths it (exponential blend toward the target velocity over a
configurable halflife), integrates it into an accumulated
`Transform3D`, and hands that off via `consume_delta()`.

**3. Internal Workflow:** `update(delta, current_frame, previous_frame,
blend)` — during a cross-fade, blends the outgoing and incoming frames'
velocities by the blend weight before smoothing; on a frame jump
(`notify_frame_jump()`), the very next `update()` tick now **adopts the
new frame's velocity immediately** rather than easing in over
`blend_halflife` — this was a verified, fixed defect this session (see
`CHANGELOG.md`): the mechanism existed but was previously never actually
consulted.

**4. Runtime Behavior:** updated once per controller tick, after playback
advances; `consume_delta()` is meant to be called once per tick by the
game's own character-movement code (via
`MotionMatchingController::consume_root_motion()`).

**5. When to Use:** always, under every `MotionMatchingController`.

**6. When NOT to Use:** n/a — not an optional component of the standard
pipeline.

**7. Inputs:** a `MotionMatchingDatabase` (for per-frame velocity data),
current/previous frame indices and blend weight (from the controller),
`notify_frame_jump(new_frame)` on a match switch.

**8. Outputs:** `consume_delta()` → a `Transform3D` displacement for the
elapsed tick; this transform is then optionally routed through
`MMMotionWarp::warp_delta()` by the controller before being handed to the
game.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `blend_halflife` | `float` | 0.08 | Smoothing rate for ordinary (non-jump) velocity changes |
| `apply_vertical` | `bool` | false | Whether vertical (Y-axis) root motion is applied |
| `correction_strength` | `float` | 0.0 | Rate at which reported position error is fed back in |

**10. Methods**
| Method | Description |
|---|---|
| `set_database(db)` | Assigns the database to read velocities from; also calls `reset()` |
| `update(delta, current_frame, previous_frame, blend)` | Per-tick integration |
| `notify_frame_jump(new_frame)` | Signals a motion-matching switch; next tick adopts the new velocity immediately |
| `report_position_error(error)` | Feeds physics-vs-animation drift back for gradual correction |
| `consume_delta()` | Returns and clears the accumulated `Transform3D` |
| `reset()` | Clears all accumulated/smoothed state |

**11. Signals:** none.

**12. Dependencies:** requires a `MotionMatchingDatabase`. Owned and
driven by `MotionMatchingController`; its output is optionally further
modified by `MMMotionWarp`.

**13. Data Flow:** database per-frame velocity → smoothing → integration
→ accumulated transform → `consume_delta()` → (optional) motion warp →
character movement.

**14. Execution Order:** `update()` after playback advance, every
controller tick; `consume_delta()` called by the game whenever it applies
movement (typically also once per physics tick).

**15. Performance Notes:** O(1) per tick — a handful of vector/quaternion
operations, no per-frame-database-size cost.

**16. Thread Safety:** main-thread only; not touched by the background
search worker.

**17. Serialization:** not itself serialized; its one tunable property
set (`blend_halflife` etc.) is typically project-configured rather than
per-instance authored.

**18. Limitations:** the fix to `notify_frame_jump()`'s immediate-adoption
behavior has never been executed/observed — it's verified by source
inspection and an authored-but-unexecuted test
(`test_root_motion_continuity.gd`), not by an actual run.

**19. Best Practices:** always call `consume_delta()` exactly once per
tick you intend to apply it — calling it twice drains the same
accumulated delta only once (the second call returns an empty/identity
transform), and skipping a tick causes displacement to silently
accumulate and apply all at once on the next call.

**20. Example:**
```gdscript
func _physics_process(delta):
    var root_delta = controller.consume_root_motion()
    global_transform = global_transform * root_delta
```

**21. Related Classes:** `MotionMatchingDatabase` (velocity source),
`MMMotionWarp` (optional further correction), `MotionMatchingController`
(owner/driver).
