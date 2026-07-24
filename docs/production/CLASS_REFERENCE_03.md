# MOTION MATCHING — Production Class Reference (Part 3 of 4)

Continued from Part 2. Same evidence labeling convention.

---

# MMTraversal (`RefCounted`)

**1. Purpose:** raycasts along the character's predicted trajectory to
detect and classify an approaching obstacle (vault, mantle, climb, slide,
roll), so locomotion can react to geometry it hasn't reached yet.

**2. Overview:** an opt-in subsystem — a `MotionMatchingController` only
probes for obstacles if an `MMTraversal` instance has been explicitly
assigned via `set_traversal()`; leaving it unassigned costs nothing.

**3. Internal Workflow:** `probe(space_state, character_transform,
trajectory)` casts along the predicted path; on detection, records an
entry point, top point, exit point, obstacle height/depth, and surface
angle, and computes a required-tags bitmask for the search filter.
`clear()` resets all of this to "nothing detected."

**4. Runtime Behavior:** probed once per controller tick, only when
grounded and a character node resolves (airborne probing wouldn't be
meaningful).

**5. When to Use:** parkour/traversal-capable characters that should
automatically react to vaultable/climbable geometry.

**6. When NOT to Use:** characters with no traversal moveset — leave
`_traversal` unassigned rather than instantiating an unused one.

**7. Inputs:** a `PhysicsDirectSpaceState3D` (from the character's
`World3D`), the character's transform, the `MMTrajectory` predictor.

**8. Outputs:** a detected `TraversalType` enum value (or `TRAVERSAL_NONE`),
`get_top_point()` (used as the `traversal_requested` signal's target and,
typically, the motion-warp target), `get_required_tags()` (fed into the
next search's filter).

**9. Properties:** raycast tuning properties (exact set: see
`include/traversal.hpp` for authoritative property names/defaults — not
independently re-listed here to avoid restating unverified specifics).

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `probe(space, transform, trajectory)` | Runs detection for this tick | `TraversalType` |
| `clear()` | Resets detected state | — |
| `get_detected_type()` | Current detection state | `int` (`TraversalType`) |
| `get_top_point()` / `get_entry_point()` / `get_exit_point()` | Detected geometry points | `Vector3` |
| `get_required_tags()` | Tags the next search filter should require | `int` |

**11. Signals:** none of its own — the controller emits
`traversal_requested` on its behalf when this class detects something.

**12. Dependencies:** requires a `PhysicsDirectSpaceState3D` (via the
character's `World3D`) and an `MMTrajectory`. Optionally referenced by
`MotionMatchingController`.

**13. Data Flow:** predicted trajectory + physics world → raycast →
detected obstacle type/geometry → controller's search filter bias +
`traversal_requested` signal → gameplay response (e.g., triggering a
motion warp toward `get_top_point()`).

**14. Execution Order:** probed once per controller tick, after
trajectory update, before the search filter is built.

**15. Performance Notes:** one or more raycasts per probe — real physics
queries, not free; this is why probing is gated to grounded-only and
opt-in.

**16. Thread Safety:** main-thread only — physics queries in Godot are
not safe to run from arbitrary background threads **[EXPECTED, standard
Godot physics threading constraint]**.

**17. Serialization:** typically configured once per character archetype;
not usually saved as a standalone shared asset, though it is `RefCounted`
and could be.

**18. Limitations:** integrated into the controller this session but
**never executed against real physics geometry** — logically wired
(verified by source inspection), not runtime-confirmed.

**19. Best Practices:** assign one `MMTraversal` per character that needs
it; connect to `traversal_requested` and decide gameplay-side what each
`TraversalType` should trigger (this addon deliberately does not hardcode
that decision).

**20. Example:**
```gdscript
var traversal := MMTraversal.new()
controller.set_traversal(traversal)
controller.traversal_requested.connect(func(type, target):
    controller.begin_warp(Transform3D(Basis(), target))
)
```

**21. Related Classes:** `MMTrajectory` (input), `MMMotionWarp` (typical
downstream response), `MotionMatchingController` (owner).

---

# MMMotionWarp (`RefCounted`)

**1. Purpose:** bends a clip's root motion toward a target it wasn't
authored for — a vault animation captured against one obstacle, applied
to a thousand different obstacle placements, without needing a clip per
placement.

**2. Overview:** the correction is spread over a time window and applied
to the root *delta*, never distorting the sampled pose itself; multiple
windows can exist (`add_window()`), each with its own start/end time and
target transform.

**3. Internal Workflow:** `begin(start_transform)` records where warping
begins; `warp_delta(delta, current, time, delta_time)` computes how much
correction remains for the active window and blends it into the raw root
delta; `end()` deactivates.

**4. Runtime Behavior:** consulted every tick by
`MotionMatchingController::consume_root_motion()`, but only produces a
non-identity correction while `is_active()` is true and a window covers
the current playback time.

**5. When to Use:** any clip that needs to reach a specific,
gameplay-determined target — vaults, mantles, distance-matched
stops/starts, ledge grabs.

**6. When NOT to Use:** ordinary locomotion with no specific target —
leave unassigned (opt-in, null by default costs nothing).

**7. Inputs:** a start transform (`begin()`), one or more windows
(`add_window()`), the raw root delta and current playback time (per
`warp_delta()` call, supplied by the controller).

**8. Outputs:** a corrected `Transform3D` delta.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `translation_limit` | `float` | 2.5 | Caps how far a single warp can translate, as a safety bound |
| `rotation_limit` | `float` | π (3.14159) | Caps warp rotation |

**10. Methods**
| Method | Description |
|---|---|
| `begin(start_transform)` | Starts warping from this transform |
| `end()` | Stops warping |
| `is_active()` | Whether currently warping |
| `add_window(start_time, end_time, target, warp_position=true, warp_rotation=true)` | Adds a correction window |
| `clear_windows()` | Removes all windows |
| `get_window_count()` | Number of active windows |
| `warp_delta(delta, current, time, delta_time)` | Per-tick corrected delta |
| `find_time_for_distance(distance_curve, distance)` (static) | Distance-matching: finds the playback time whose remaining travel best matches a target distance — used to start a stop/takeoff at exactly the right moment |

**11. Signals:** none.

**12. Dependencies:** consulted by `MotionMatchingController::consume_root_motion()`.
Typically triggered from an `MMTraversal` detection or other gameplay
logic supplying the target.

**13. Data Flow:** gameplay target → `begin_warp()` (controller
convenience) or manual `begin()`/`add_window()` → per-tick
`warp_delta()` correction → applied root motion.

**14. Execution Order:** `begin()`/`add_window()` triggered by gameplay
logic (often in response to `traversal_requested`); `warp_delta()` called
every tick thereafter until `end()`.

**15. Performance Notes:** O(number of windows) per tick to find the
active one — expected to be a small number in practice.

**16. Thread Safety:** main-thread only, called synchronously from
`consume_root_motion()`.

**17. Serialization:** not typically saved as a standalone asset — usually
created and configured at runtime in response to gameplay events.

**18. Limitations [SOURCE, explicitly documented as unsolved]:** no
automatic handling if a clip switch happens mid-warp — old windows
authored for the previous clip's timeline would misapply against the new
clip's playback time. Caller's responsibility to call `end()`/`clear_windows()`
appropriately if this matters for your use case.

**19. Best Practices:** call `end()` (or start a fresh `begin()`) whenever
the underlying clip changes, if you're not certain the warp windows still
apply.

**20. Example:**
```gdscript
controller.begin_warp(ledge_transform)
# ... clip plays, root motion bends toward ledge_transform ...
controller.end_warp()
```

**21. Related Classes:** `MotionMatchingController` (driver),
`MMTraversal` (typical target source), `MMRootMotion` (the delta being
corrected).

---

# MMSkeletonProfile (`Resource`)

**1. Purpose:** universal rig detection — maps 22 semantic bone roles
(`MMBoneRole` enum) to whatever names a specific skeleton actually uses,
so nothing else in the framework needs to know or guess bone names.

**2. Overview:** three-pass detection, strongest evidence first: (1)
structural graph analysis (leg/arm/spine/head chains identified from
topology alone — works even on anonymously-named bones), (2)
normalized-token name matching (disambiguates left/right, recognizes
Mixamo/UE/Rokoko/Blender/mocap conventions by token, not exact string), (3)
manual overrides (always win, persist across re-detection).

**3. Internal Workflow:** `auto_detect(skeleton)` builds an internal
graph (parents/children/depth/rest transforms), finds leg chains via
deepest-leaf/chain-length analysis, disambiguates sides via name-token
hints, and fills every role it can determine.

**4. Runtime Behavior:** typically run once at build/setup time, not
every frame — its output (bone names) is baked into `MMFeatureSchema` and
`MotionMatchingDatabase`, not re-queried at runtime.

**5. When to Use:** once per distinct skeleton/rig you want to support.

**6. When NOT to Use:** don't re-run `auto_detect()` every frame — it's a
one-time (or "run when the rig changes") operation.

**7. Inputs:** a `Skeleton3D`.

**8. Outputs:** `is_detected()`, `has_role(role)`, `get_missing_roles()`,
`get_bone_name(role)`, `get_detection_report()` (human-readable summary
string), convenience sets (`get_default_pose_bones()`, `get_foot_bones()`,
`get_hand_bones()`).

**9. Properties**
| Name | Type | Purpose |
|---|---|---|
| `bones` | `PackedStringArray` | All bone names in the skeleton, as detected |
| `spine_chain` | `PackedStringArray` | Detected spine chain |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `auto_detect(skeleton)` | Runs the full 3-pass detection | `bool` (false only if skeleton is empty/too small for a biped) |
| `set_bone_name(role, name)` / `get_bone_name(role)` | Manual override / lookup | — / `String` |
| `find_bone(skeleton, role)` | Resolves a role to a live bone index on a given skeleton | `int` |
| `swap_sides()` | Swaps every left/right role assignment | — |
| `has_role(role)` / `get_missing_roles()` | Coverage queries | `bool` / `PackedStringArray` |
| `get_role_name(role)` (static) | Human-readable role name | `String` |

**11. Signals:** none.

**12. Dependencies:** requires a `Skeleton3D`. Consumed by
`MMFeatureSchema::apply_skeleton_profile()` and `MMFeatureExtractor`.

**13. Data Flow:** skeleton → structural analysis → name matching →
manual override → resolved role→name mapping → schema/extractor.

**14. Execution Order:** build/setup time only.

**15. Performance Notes:** O(bone count) graph analysis, run once — not
a per-frame cost.

**16. Thread Safety:** main-thread/editor-time use; no concurrency
concerns documented.

**17. Serialization:** `.tres`/`.res`; manual overrides persist across
re-detection (`_locked` bitmask), so a saved profile safely survives
calling `auto_detect()` again.

**18. Limitations:** requires a minimum bone count to attempt biped
detection; returns `false` rather than a partial guess if the skeleton is
too small/empty.

**19. Best Practices:** review `get_detection_report()` and
`get_missing_roles()` after auto-detection on a new/unusual rig before
trusting it blindly — the report exists specifically so this can be a
reviewed step, not a black box.

**20. Example:**
```gdscript
var profile := MMSkeletonProfile.new()
if not profile.auto_detect(skeleton):
    push_error("Skeleton too small for biped detection")
else:
    print(profile.get_detection_report())
```

**21. Related Classes:** `MMFeatureSchema` (its primary consumer),
`MMFeatureExtractor` (uses it indirectly through the schema).

---

# MMClipAnalyzer (`Resource`)

**1. Purpose:** classifies clips by *measured motion*, not file name — an
untitled mocap take and a clip named `LOC_RUN_FWD_01` classify identically
if they move identically, which is what makes the framework
animation-pack-agnostic.

**2. Overview:** configurable speed bands (idle/walk/jog/run, in hip
heights per second — normalized by the character's own scale) and motion
shape thresholds (turn, strafe, backward, crouch, airborne, traversal
height, start/stop delta) drive `classify()`. An optional name-rule table
exists but **ships empty by default** — an override for teams with a
strict naming convention, never a requirement.

**3. Internal Workflow:** `classify(stats, name)` consults measured
`MMClipStats` (average/peak/start/end speed, net displacement, vertical
range, total turn, lateral/backward ratios, contact ratio, longest
airborne duration, crouch ratio) against the configured thresholds; name
is only consulted if `use_name_rules` is explicitly enabled and the
dictionary is populated.

**4. Runtime Behavior:** run once per clip during database building, not
at runtime.

**5. When to Use:** always, as part of the standard build pipeline.

**6. When NOT to Use:** n/a for standard use; disable
`use_motion_analysis` only if you have a very specific reason to classify
purely by name (unusual — motion-based classification is the whole point
of this system).

**7. Inputs:** `MMClipStats` (measured from the clip by
`MMFeatureExtractor`), optionally the clip's name.

**8. Outputs:** a tags bitmask; `category_for_tags(tags)` (static) derives
a category from tags.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `idle_speed` | `float` | 0.10 | Below this (hip heights/sec) = idle |
| `walk_speed` | `float` | 1.50 | Walk band upper bound |
| `jog_speed` | `float` | 2.60 | Jog band upper bound |
| `run_speed` | `float` | 3.80 | Run band upper bound |
| `turn_threshold` | `float` | 1.05 rad | Net yaw to count as a turn |
| `strafe_ratio` | `float` | 0.55 | Lateral/total travel ratio to count as strafing |
| `backward_ratio` | `float` | 0.55 | Same, for backward motion |
| `crouch_drop` | `float` | 0.18 | Pelvis drop (hip heights) to count as crouching |
| `airborne_seconds` | `float` | 0.18 | Longest no-foot-contact duration to count as airborne |
| `traversal_height` | `float` | 0.35 | Vertical range (hip heights) suggesting a traversal clip |
| `start_stop_delta` | `float` | 0.8 | Speed change across the clip suggesting a start/stop |
| `name_rules` | `Dictionary` | empty | Optional name-based override table |
| `use_name_rules` | `bool` | false | Whether to consult `name_rules` |
| `use_motion_analysis` | `bool` | true | Whether to classify from measured motion |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `calibrate_speed_bands(speeds)` | Derives speed bands from a library's own speed distribution (quartiles, idle excluded) | — |
| `classify(stats, name)` | Core classification | `int` (tags bitmask) |
| `category_for_tags(tags)` (static) | Category from tags | `int` |
| `classify_dictionary(stats_dict, name)` | Scriptable entry point | `int` |
| `stats_to_dictionary(stats)` (static) | Converts `MMClipStats` to a `Dictionary` | `Dictionary` |

**11. Signals:** none.

**12. Dependencies:** consumed by `MMFeatureExtractor`. Its
`category_for_tags()` is also called by
`MMFeatureExtractor::guess_category_from_tags()`.

**13. Data Flow:** clip motion → `MMClipStats` → thresholds → tags →
category.

**14. Execution Order:** build/authoring time only.

**15. Performance Notes:** O(1) per clip — a fixed set of threshold
comparisons against already-computed stats, not a per-frame cost at
classification time (the stats themselves are computed by sampling the
clip, which is O(frame count), but that's the extractor's cost, not this
class's).

**16. Thread Safety:** no concurrency concerns; build-time use.

**17. Serialization:** `.tres`/`.res`; typically one shared analyzer per
project/rig-scale, reused across many clip libraries.

**18. Limitations:** thresholds are tuned defaults meant to work
reasonably across scales via hip-height normalization, but
`calibrate_speed_bands()` is recommended for unfamiliar or stylized
movement libraries rather than trusting the defaults blindly.

**19. Best Practices:** call `calibrate_speed_bands()` once per library
before building, especially for non-human-scale characters or
unconventional movement styles.

**20. Example:**
```gdscript
var analyzer := MMClipAnalyzer.new()
analyzer.calibrate_speed_bands(library_speed_samples)
extractor.set_analyzer(analyzer)
```

**21. Related Classes:** `MMFeatureExtractor` (its consumer/driver),
`MMAnimationEntry` (stores the resulting tags/category per clip).

---

# MMFeatureExtractor (`RefCounted`)

**1. Purpose:** converts any `AnimationLibrary` into a
`MotionMatchingDatabase` — the main build-time pipeline entry point.

**2. Overview:** orchestrates `MMPoseSampler` (per-clip track binding and
sampling), `MMSkeletonProfile` (rig), `MMClipAnalyzer` (classification),
and `MMFeatureSchema` (layout) — nothing pack-specific is encoded in this
class itself.

**3. Internal Workflow:** `analyze_library()` (automatic tagging pass,
explicitly meant to be *reviewed*, not blindly trusted, per its own
comment) → `build_database()` (main entry point; any clip settings not
explicitly provided are filled in by analysis).

**4. Runtime Behavior:** build/editor-time only — never invoked as part
of normal gameplay.

**5. When to Use:** whenever building or rebuilding a database from source
animation clips.

**6. When NOT to Use:** not for runtime use — this is an authoring-time
tool.

**7. Inputs:** a `Skeleton3D`, an `AnimationLibrary`, optional per-clip
settings `Dictionary`.

**8. Outputs:** a `MotionMatchingDatabase`; `analyze_animation()`/
`analyze_library()` return `Dictionary`s of measured stats/suggested
tags for review.

**9. Properties**
| Name | Type | Default | Purpose |
|---|---|---|---|
| `schema` | `MMFeatureSchema` | null | Output layout |
| `profile` | `MMSkeletonProfile` | null | Rig detection to use |
| `analyzer` | `MMClipAnalyzer` | null | Classifier to use |
| `sample_rate` | `float` | 30.0 | Sampling frequency for the database |
| `foot_contact_speed_ratio` | `float` | 0.35 | Hip heights/sec below which a foot counts as planted |
| `foot_contact_height_ratio` | `float` | 0.12 | Height (hip heights) below which a foot counts as planted |
| `auto_detect_profile` | `bool` | true | Whether to run skeleton detection automatically |
| `auto_configure_schema` | `bool` | true | Whether to auto-fill the schema from detection |

**10. Methods**
| Method | Description | Returns |
|---|---|---|
| `analyze_animation(skeleton, animation)` | Measures one clip's motion | `Dictionary` |
| `analyze_library(skeleton, library)` | Automatic tagging pass for a whole library | `Dictionary` |
| `build_database(skeleton, library, clip_settings)` | Main entry point | `MotionMatchingDatabase` |
| `append_animation(database, skeleton, animation, name, library, category, tags)` | Adds one clip to an existing database | `bool` |
| `guess_tags_from_name(name)` (static) | Name-only tag guess, delegates to `MMClipAnalyzer` with motion analysis off | `int` |
| `guess_category_from_tags(tags)` (static) | Delegates to `MMClipAnalyzer::category_for_tags()` | `int` |
| `get_progress()` / `get_progress_label()` | Build progress, for editor progress bars | `float` / `String` |

**11. Signals:** none.

**12. Dependencies:** requires `MMFeatureSchema`, `MMSkeletonProfile`,
`MMClipAnalyzer`. Uses `MMPoseSampler` internally. Produces
`MotionMatchingDatabase`.

**13. Data Flow:** see `docs/production/WORKFLOW.md`'s database
generation pipeline section for the full stage-by-stage breakdown.

**14. Execution Order:** build/editor time only.

**15. Performance Notes:** cost scales with total sampled frames across
the whole library (clip count × clip length × sample rate); the
per-clip build loop is single-threaded despite the codebase being
"structured to allow" parallelizing across clips **[SOURCE, a known,
documented limitation — not yet implemented]**.

**16. Thread Safety:** no concurrency in this class itself; not called
from the background search thread.

**17. Serialization:** not itself serialized; its output
(`MotionMatchingDatabase`) is.

**18. Limitations:** single-threaded build loop (see Performance Notes).

**19. Best Practices:** review `analyze_library()`'s suggested
tags/categories before committing to `build_database()` — it's designed
as a reviewable intermediate step, not a black box.

**20. Example:**
```gdscript
var extractor := MMFeatureExtractor.new()
extractor.set_schema(schema)
var database := extractor.build_database(skeleton, library, {})
database.finalize(schema.get_dimension())
```

**21. Related Classes:** `MMPoseSampler`, `MMSkeletonProfile`,
`MMClipAnalyzer`, `MMFeatureSchema` (all consumed), `MotionMatchingDatabase`
(produced).

---

# MMPoseSampler (`RefCounted`)

**1. Purpose:** resolves an `Animation` against a `Skeleton3D` once, then
samples it cheaply — track lookup by bone name happens a single time in
`bind()`; after that, a sample is an array index, not a string compare.

**2-8. Overview/Workflow/Behavior/Use/Inputs/Outputs:** see
`CLASS_REFERENCE.md`'s condensed entry — this is purely a build-time
performance/convenience utility used internally by `MMFeatureExtractor`,
not typically used directly by addon consumers.

**Notable detail [SOURCE]:** track paths are matched on their *last
subname* (the bone), so a clip authored against
`"Armature/Skeleton3D:Hips"` and one authored against
`"%GeneralSkeleton:Hips"` both bind without path rewriting. When a clip
has no root track, the pelvis is projected onto the ground plane and used
instead — the mechanism that makes in-place animation packs (no
translating root bone) work without manual conversion.

**9-10. Properties/Methods:** `bind(skeleton, animation, root_bone,
pelvis_bone)` → `bool`; `sample(time)`; `get_model_transform(bone)` /
`get_local_transform(bone)` / `get_root_transform()`; `sample_bone(name,
time)` → `Dictionary` (scriptable convenience).

**12. Dependencies:** used internally by `MMFeatureExtractor`.

**16. Thread Safety:** build-time, main-thread use; not thread-shared.

**18. Limitations:** build-time only tool, not designed for runtime
sampling of arbitrary animations.

**21. Related Classes:** `MMFeatureExtractor` (its sole consumer).

---

# MMAnimationLibraryTools (`RefCounted`)

**1. Purpose:** a small set of static utility functions for
library-level authoring tasks that don't belong on any single other
class.

**9-10. Methods (all static)**
| Method | Description | Returns |
|---|---|---|
| `auto_tag_library(library)` | Bulk-suggests tags/category per clip using `MMFeatureExtractor::guess_tags_from_name()` | `Dictionary` |
| `validate_library(library, skeleton, schema)` | Pre-flight checks: no root track, missing tracked bones, zero-length clips — catches problems before hours are spent building | `Array` of issue dictionaries |
| `merge_libraries(libraries, prefixes)` | Flattens several libraries into one, with optional per-library name prefixes (e.g., combining an unarmed set, a rifle set, and a traversal set) | `AnimationLibrary` |

**12. Dependencies:** `validate_library()` requires `MMFeatureSchema`
(for the expected root bone/tracked bones); `auto_tag_library()` calls
into `MMFeatureExtractor`'s static helpers.

**15. Performance Notes:** all three functions are O(clip count) or
O(clip count × track count) — linear scans, not expensive relative to
the build process they support.

**18. Limitations:** `validate_library()`'s checks are structural
(missing tracks/bones, zero length) — they don't evaluate motion
quality.

**19. Best Practices:** run `validate_library()` before every database
build, especially on a new or unfamiliar animation source.

**20. Example:**
```gdscript
var issues = MMAnimationLibraryTools.validate_library(library, skeleton, schema)
for issue in issues:
    print("%s: %s" % [issue["severity"], issue["message"]])
```

**21. Related Classes:** `MMFeatureExtractor`, `MMFeatureSchema`.

---

# MMAnimationEntry (`Resource`)

**1. Purpose:** one row of per-clip metadata inside a database — name,
library name, tags, category, length.

**9. Properties**
| Name | Type | Purpose |
|---|---|---|
| `animation_name` | `String` | The clip's name within its library |
| `library_name` | `String` | Which library it came from (for qualified naming) |
| `tags` | `int` (bitmask) | `MMTag` values assigned by `MMClipAnalyzer` |
| `category` | `int` | `MMCategory` value |

**10. Methods:** `get_qualified_name()` → computed `"library/name"`
string (not stored, derived on demand).

**12. Dependencies:** one instance per clip inside a
`MotionMatchingDatabase`; referenced by frame-index-to-clip lookups
throughout search/playback code.

**21. Related Classes:** `MotionMatchingDatabase` (container),
`MMClipAnalyzer` (assigns tags/category at build time).

---

# MMProfiler (`RefCounted`)

**1. Purpose:** search-time percentiles, clip-switch-rate, and
budget-overrun counting, in one report meant to be dropped directly into
a debug UI.

**2. Overview:** its own doc comment states the report dictionary is
meant for "one dictionary for the editor panel and for
`get_debug_info()`" — this session's controller integration finally
wired that up as intended.

**3-4. Internal Workflow/Runtime Behavior:** fed via `record()` (called
after every real, non-cached search) and `record_switch()` (called after
every accepted clip switch); never fed by cache hits, deliberately, to
avoid diluting the percentiles with near-zero-cost samples.

**8. Outputs:** `get_report()` → `Dictionary` of search-time percentiles,
switch count, budget-overrun count.

**10. Methods**
| Method | Description |
|---|---|
| `record(time_usec, frames_compared, candidates, nodes, budget_exceeded)` | Feeds one real search's stats |
| `record_switch()` | Increments the switch counter |
| `get_report()` | The full statistics dictionary |

**12. Dependencies:** instantiated automatically by
`MotionMatchingController` (always present, unlike the opt-in
traversal/warp subsystems); fed by `_run_search()`/`_apply_match()`.

**16. Thread Safety:** fed only from the main thread (real searches are
recorded after either the sync path completes or an async response is
polled — both main-thread operations); not touched by the background
worker thread directly.

**18. Limitations:** does not track the 4 additional phase timings
(query build, continuation eval, switch apply, total update) added this
session — those live as separate plain members on the controller instead,
surfaced through `get_debug_info()` directly, since they don't fit this
class's search-shaped internal `Sample` struct.

**21. Related Classes:** `MotionMatchingController` (owner/feeder).

---

# MMIKSolver (`RefCounted`)

**1. Purpose:** a stateless solver library shared by the IK modifiers and
traversal, so three different consumers share one implementation instead
of three.

**10. Methods (all static)**
| Method | Description |
|---|---|
| `solve_two_bone(skeleton, root, mid, tip, target, pole, weight)` | Analytic two-bone solve — exact, one iteration, no drift; "the only one that should touch legs" per its own comment |
| `solve_fabrik(chain, target, iterations, tolerance)` | Iterative solve for longer chains (spines, tails, tentacles, shifting-anchor ledge grabs) |
| `solve_ccd(skeleton, chain, tip, target, iterations, weight)` | Alternative iterative solver |
| `look_at_bone(skeleton, bone, target, forward_axis, max_angle, weight)` | Cone-limited direction solve for look/aim |
| `set_bone_global_basis(skeleton, bone, basis)` | Applies a model-space rotation via the bone's parent space |

**12. Dependencies:** used internally by `MMFootIKModifier`,
`MMAimIKModifier`, and (for its raycast-and-solve pattern) conceptually
related to `MMTraversal`'s geometry awareness.

**16. Thread Safety:** stateless static functions — safe to call from
anywhere, though in practice only ever called from the main thread as
part of `_process_modification()`.

**21. Related Classes:** `MMFootIKModifier`, `MMAimIKModifier`.
