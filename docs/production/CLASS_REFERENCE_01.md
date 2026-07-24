# MOTION MATCHING — Production Class Reference (Part 1 of 3)

Official reference manual for the Motion Matching GDExtension. Written for
a reader who has never seen the source. No demo content is referenced
anywhere in this document set — this is production addon documentation
only.

**Evidence labeling used throughout:**
- **[SOURCE]** — read directly from the header/implementation file
- **[EXPECTED]** — standard, well-established Godot/C++ behavior relied on
  but not independently re-executed in this environment
- **[INFERENCE]** — judgment, recommendation, or interpretation, not a
  directly-quotable fact
- Any statement about what happens when the code *runs* (as opposed to
  what the code *says*) that has not actually been executed is marked
  **[EXPECTED]**, never presented as confirmed.

---

# MotionMatchingController (`Node`)

## 1. Purpose
Drives data-driven character locomotion by continuously searching a
precomputed motion database for the frame of animation that best matches
what the character is currently trying to do, and playing from that exact
point — never from the start of a clip. **[SOURCE]** It exists because
traditional animation state machines require an explicit transition for
every pair of states a designer anticipates; motion matching instead
treats the entire clip library as one continuous searchable space, so new
combinations of speed/direction/stance are handled automatically as long
as the underlying clips exist. **[INFERENCE, standard motion-matching
rationale, consistent with this project's own doc comments]**

## 2. Overview
This is the single orchestrating node of the whole addon. It owns a
trajectory predictor, a root-motion integrator, the KD-tree search
wrapper, a result cache, a background search worker, and a profiler; it
optionally references a traversal detector and a motion-warp instance.
Every other class in this addon exists either to feed this class data or
to be driven by its decisions. **[SOURCE]**

## 3. Internal Workflow
Per physics tick **[SOURCE, traced from `update()`'s exact statement order]**:
1. Advance internal timers (search interval, clip time, switch cooldown,
   landing timer, category lock timer, time-since-grounded).
2. Update the trajectory predictor from current position/velocity/facing
   and the character's desired velocity/facing.
3. If a traversal instance is assigned and the character is grounded,
   probe for an obstacle.
4. Advance clip playback time and cross-fade blend weight.
5. If async search is enabled, poll for a finished background result and
   consume it if present.
6. If a search is due (interval elapsed or forced), run one: build the
   query vector, evaluate the cost of simply continuing the current clip,
   build a search filter (locked category, else traversal bias, else
   jump/airborne, else landing, else default grounded locomotion, in that
   priority order), check the cache, then either reuse a cache hit or
   perform a real KD-tree search (synchronously or by submitting to the
   background worker).
7. Integrate root motion for this tick from the current (and, mid-blend,
   previous) frame's baked velocity.

## 4. Runtime Behavior
Runs automatically every `_physics_process()` tick once `_ready()` has
completed, **unless** the node is running inside the editor
(`Engine::is_editor_hint()`), in which case both process callbacks are
disabled entirely. **[SOURCE]** A search does not necessarily run every
tick — `search_interval` (from the assigned `MotionMatchingResource`)
controls the minimum time between real searches; `0` means "every tick is
eligible." **[SOURCE]**

## 5. When to Use
Any humanoid character (player-controlled or AI) where locomotion needs to
cover a continuous range of speeds/directions/stances from a moderately
large clip library (tens to hundreds of clips), and where the value of
motion matching — natural blending between arbitrary states without
hand-authored transitions — outweighs its cost (a larger, curated clip
library and a database-build step). **[INFERENCE]**

## 6. When NOT to Use
A character with only a handful of discrete animation states and no need
for continuous speed/direction variation is usually better served by a
conventional `AnimationTree` state machine or blend space — simpler to
author, simpler to debug, and without the up-front cost of building and
tuning a motion database. **[INFERENCE]**

## 7. Inputs
- **`resource`** (`MotionMatchingResource`) — required; bundles the
  database, schema, and cost function.
- **`character_path`** (`NodePath`) — optional; if empty, the controller's
  own parent node is used as the character, provided it is a `Node3D`.
- Every tick: **desired velocity** and **desired facing** (`Vector3`),
  set by the game's own input-handling script — the controller does not
  read input directly. **[SOURCE]**
- Optional: an `AnimationTree` sibling/ancestor (auto-bound), an
  `MMTraversal` instance, an `MMMotionWarp` instance.

## 8. Outputs
- `consume_root_motion()` — a `Transform3D` delta to apply to the
  character each tick.
- `get_debug_info()` — a `Dictionary` of match/search/profiler state (see
  Properties/Methods below for the full key list).
- Signals: `animation_changed`, `traversal_requested` (both actually
  emitted — **[SOURCE]**), and `search_completed` (declared but **never
  emitted anywhere in the implementation** — **[SOURCE, verified by
  exhaustive grep]** — do not build logic that waits for this signal; it
  currently never fires).

## 9. Properties
| Name | Type | Default | Purpose | Runtime effect | Performance | Serialization |
|---|---|---|---|---|---|---|
| `resource` | `MotionMatchingResource` | null | Database/schema/cost bundle | Reassigning triggers a full resync + rebuild | Rebuild cost scales with database size **[INFERENCE]** | Saved in scene |
| `character_path` | `NodePath` | empty | Which node is "the character" | Resolved once in `_ready()`/on change | Negligible | Saved in scene |
| `auto_update` | `bool` **[SOURCE — member `_auto_update`]** | true | Whether `_physics_process` is auto-enabled | Controls `set_physics_process()` | Negligible | Saved in scene |
| `async_search` | `bool` **[SOURCE — member `_async_search`]** | false | Enables the background search worker | Starts/stops one `std::thread` | Removes tree-descent cost from the main thread at the cost of one-tick result latency | Saved in scene |

*(Tuning properties such as `search_interval`, `blend_time`,
`minimum_blend_time`, `switch_cooldown` live on `MotionMatchingResource`,
not on this node directly — see that class below. This avoids duplicating
the same properties on every controller instance sharing one resource.
**[SOURCE]**)*

## 10. Methods
| Method | Description | Params | Returns | Notes |
|---|---|---|---|---|
| `set_resource(res)` | Assigns/swaps the bundle | `MotionMatchingResource` | — | Stops the async worker *before* resyncing (fixed this project; previously a verified UAF race existed here) |
| `set_character_path(path)` | Points at a specific character node | `NodePath` | — | |
| `set_desired_velocity(v)` / `set_desired_facing(v)` | Feeds intent for this tick | `Vector3` | — | Call every frame from your input script; the controller does not persist "intent" across ticks on its own |
| `get_debug_info()` | Full runtime state dump | — | `Dictionary` | See key list below |
| `get_debug_trajectory()` | Points for external debug drawing | — | `PackedVector3Array` | |
| `get_profiler()` | The always-present `MMProfiler` | — | `Ref<MMProfiler>` | |
| `set_traversal(t)` / `get_traversal()` | Opt-in traversal detector | `Ref<MMTraversal>` | — / `Ref<MMTraversal>` | Null = disabled, zero cost |
| `set_motion_warp(w)` / `get_motion_warp()` | Opt-in motion warp | `Ref<MMMotionWarp>` | — / `Ref<MMMotionWarp>` | Null = disabled |
| `begin_warp(target)` | Opens a default warp window to end of current clip | `Transform3D` | — | Call `get_motion_warp().add_window()` first if you need custom window timing |
| `end_warp()` | Ends the active warp | — | — | |
| `is_warping()` | Whether a warp is currently active | — | `bool` | |
| `consume_root_motion()` | Root delta for this tick, warp-corrected if active | — | `Transform3D` | Call once per physics tick; each call drains the accumulated delta |
| `rebuild()` | Rebuilds the KD-tree, clears the cache, restarts the async worker | — | — | Call after swapping the database directly (rare — normally `set_resource()` handles this) |
| `lock_category(category, duration)` / `release_category()` | Forces the search filter to one category for a time | `int, float` | — | Used internally for the airborne state after a jump; also callable from gameplay |

**`get_debug_info()` key list [SOURCE]:** `frame_index`, `animation_id`,
match cost fields, `left_foot_contact`, `right_foot_contact`,
`cache_hits`, `cache_misses`, `search_time_usec`, `query_build_usec`,
`continuation_eval_usec`, `switch_apply_usec`, `update_total_usec`,
`profiler` (nested `MMProfiler` report), `traversal_active`,
`traversal_type`, `warp_active`.

**Common mistakes [INFERENCE]:** forgetting to call `consume_root_motion()`
every tick (root motion silently accumulates and then jumps once finally
consumed); expecting `search_completed` to fire (it doesn't — see Outputs);
calling `set_resource()` from a background thread or timer callback
without confirming it runs on the main thread **[EXPECTED — nothing in
this class's design supports concurrent external calls]**.

## 11. Signals
| Signal | Purpose | Params | When emitted | Example |
|---|---|---|---|---|
| `animation_changed` | Notifies that a new clip/time was matched and accepted | `clip: StringName, time: float, cost: float` | Inside `_apply_match()`, once per accepted switch **[SOURCE]** | `controller.animation_changed.connect(func(clip, t, cost): print(clip))` |
| `traversal_requested` | Reports a detected traversal obstacle | `traversal_type: int, target: Vector3` | Inside `_evaluate_traversal()`, when `MMTraversal` detects something **[SOURCE]** | `controller.traversal_requested.connect(_on_traversal)` |
| `search_completed` | *(declared, never emitted)* | `debug_info: Dictionary` | **Never** — confirmed by exhaustive grep across the implementation file **[SOURCE]** | Do not rely on this signal |

## 12. Dependencies
Depends on: `MotionMatchingResource` (required), `AnimationTree` (for
actual playback — via `AnimationNodeMotionMatching`), a `Node3D` character.
Optionally depends on: `MMTraversal`, `MMMotionWarp`. Is depended on by:
`AnimationNodeMotionMatching` (reads matched clip/time), `MMDebugDraw`
(reads trajectory), gameplay scripts driving `MMFootIKModifier`/
`MMAimIKModifier` from this controller's contact/aim data.

## 13. Data Flow
Character intent (in) → trajectory → query → search (cache-checked) →
match result → hysteresis check → accepted match → root motion + animation
node (out). See `docs/production/SYSTEMS.md`'s Motion Matching section for
the full diagram.

## 14. Execution Order
`_ready()` (resolve character → sync from resource → bind AnimationTree →
rebuild → enable physics processing) → per-tick `_physics_process()` (see
Section 3) → `_notification(NOTIFICATION_EXIT_TREE)` (stops the
background worker before the node is destroyed). **[SOURCE]**

## 15. Performance Notes
Real search cost is dominated by the KD-tree descent (see `MMPoseSearch`
below); a cache hit skips it entirely. `search_interval > 0` is the
primary lever for reducing search frequency. `async_search` moves the
descent off the main thread at the cost of one tick of result latency.
No performance numbers in this document are measured — they are
structural/algorithmic observations from source, not benchmarks
**[INFERENCE — no execution has occurred]**.

## 16. Thread Safety
Owns exactly one background thread (`MMSearchWorker`). All *public* methods
are expected to be called from the main thread only **[EXPECTED]** —
nothing in the class supports concurrent external calls. The one verified,
now-fixed race in this class's history: `set_resource()` could destroy a
`Ref` (`_cost_function`) the worker thread still held a raw pointer to,
if called while an async search was in flight. Fixed by moving
`_worker.stop()` to the top of `set_resource()`.

## 17. Serialization
Standard `Node` — its exported properties (`resource`, `character_path`,
etc.) save as part of the scene (`.tscn`); no custom serialization exists
or is needed.

## 18. Limitations
- `search_completed` signal is dead (see Signals).
- Cache size (1024) and quantization step (0.15) are fixed internal
  constants, not exposed as tunable properties.
- No automatic handling if a clip switch happens while a motion warp is
  active mid-window — explicitly left as caller responsibility.
- Database build loop (used offline, not by this class directly, but
  feeding it) is single-threaded.

## 19. Best Practices
Set desired velocity/facing every frame before relying on
`consume_root_motion()`'s output that same frame **[INFERENCE]**. Use
`get_debug_info()` during tuning, not every frame in a shipped build if
avoidable (it assembles a full `Dictionary`, including a nested
`MMProfiler` report, every call). Prefer `async_search` for characters
where a search-tree descent would otherwise compete with a tight physics
budget; accept the one-tick latency tradeoff.

## 20. Example
```gdscript
@onready var controller: MotionMatchingController = $MotionMatchingController

func _physics_process(delta):
    var input_dir = Vector3(Input.get_axis("move_left", "move_right"), 0,
            Input.get_axis("move_forward", "move_back"))
    controller.set_desired_velocity(input_dir.normalized() * 4.0)
    controller.set_desired_facing(input_dir.normalized() if input_dir.length() > 0.1 else global_transform.basis.z)
    var delta_transform = controller.consume_root_motion()
    global_transform *= delta_transform
```

## 21. Related Classes
`MotionMatchingResource` (its data source), `AnimationNodeMotionMatching`
(its playback output), `MMTrajectory`/`MMRootMotion` (owned subsystems),
`MMTraversal`/`MMMotionWarp` (optional owned subsystems),
`MMFootIKModifier`/`MMAimIKModifier` (siblings driven by this
controller's data, not owned by it).

---

# MotionMatchingResource (`Resource`)

## 1. Purpose
Bundles every piece of data a `MotionMatchingController` needs into one
saveable asset, so a character variant (different rig, different move
set) is a single resource swap rather than reconfiguring a node's many
individual properties. **[SOURCE]**

## 2. Overview
A thin container resource: it holds references to a
`MotionMatchingDatabase`, an `MMFeatureSchema`, an `MMCostFunction`, an
`AnimationLibrary`, and a set of tuning numbers (search interval, blend
times, switch cooldown, trajectory halflives, max speed, quality level,
debug toggle). It has one piece of real logic of its own: `validate()`.

## 3. Internal Workflow
`validate()` runs a fixed sequence of checks and returns as soon as a
fatal one is hit: missing database → (return early) — otherwise checks
zero frame count, zero dimension, format-version compatibility, and
schema/database dimension agreement, accumulating warnings/errors into an
array rather than stopping at the first one found (except the missing-
database case, since nothing else can be meaningfully checked without one).
**[SOURCE]**

## 4. Runtime Behavior
Read once by the controller in `_sync_from_resource()` (called from
`_ready()` and again whenever `set_resource()` is invoked). Not
continuously polled — the controller caches what it needs
(`_database`, `_schema`, `_cost_function`) as its own members after
syncing. **[SOURCE]**

## 5. When to Use
As the one asset per character variant that an artist/designer swaps to
change movesets — e.g., a "heavy armor" variant vs. a "light" variant of
the same character, each with its own database and tuning. **[INFERENCE]**

## 6. When NOT to Use
Don't put per-instance runtime state here — this is a shared, saveable
asset; multiple controller instances can (and are expected to) reference
the same one. **[INFERENCE]**

## 7. Inputs
Assigned by hand (in the editor or by script): `animation_library`,
`database`, `schema`, `cost_function`, plus the tuning numbers.

## 8. Outputs
`validate()` → `Array` of `{severity: String, clip: String, message: String}`
dictionaries.

## 9. Properties
| Name | Type | Default | Purpose | Serialization |
|---|---|---|---|---|
| `animation_library` | `AnimationLibrary` | null | Source clips (build-time reference; not required at pure runtime if a database already exists) | Saved |
| `database` | `MotionMatchingDatabase` | null | The built, searchable data | Saved |
| `schema` | `MMFeatureSchema` | null | Feature layout; falls back to the database's embedded schema, then a default, if unset | Saved |
| `cost_function` | `MMCostFunction` | null (auto-instantiated if missing) | Search distance metric | Saved |
| `search_interval` | `float` | project-configured **[SOURCE — value not independently re-verified this pass]** | Minimum time between real searches | Saved |
| `blend_time` / `minimum_blend_time` / `switch_cooldown` | `float` | project-configured | Cross-fade timing / anti-thrash timing | Saved |
| `quality` | `int` (`MMQuality`) | — | LOD level for the search dimension bound | Saved |
| `debug_enabled` | `bool` | false | Toggle for verbose debug behavior | Saved |

## 10. Methods
| Method | Description | Returns |
|---|---|---|
| `validate()` | Structured pre-flight check | `Array` of issue dictionaries |
| standard `set_*`/`get_*` for every property above | — | — |

## 11. Signals
None declared. **[SOURCE]**

## 12. Dependencies
Referenced by `MotionMatchingController`. References
`MotionMatchingDatabase`, `MMFeatureSchema`, `MMCostFunction`,
`AnimationLibrary`.

## 13. Data Flow
Authored/built (editor or script) → saved as `.tres` → loaded and read by
a controller's `_sync_from_resource()`.

## 14. Execution Order
Not itself tick-driven; read at controller `_ready()`/`set_resource()`
time only.

## 15. Performance Notes
`validate()`'s cost is proportional to the number of checks (constant),
not the database size — it inspects metadata (frame count, dimension,
format version), not per-frame data.

## 16. Thread Safety
No threading concerns of its own; read by the controller on the main
thread. **[EXPECTED]**

## 17. Serialization
Standard `Resource` — every `ADD_PROPERTY`'d field round-trips through
`.tres`/`.res` automatically.

## 18. Limitations
`validate()` checks structural consistency, not match quality — an empty
array does not guarantee the database will produce good search results,
only that its pieces are internally consistent. **[SOURCE, stated
explicitly in its own doc comment]**

## 19. Best Practices
Call `validate()` once after building or loading a database (e.g., in an
editor tool script or a startup check), not every frame. Treat "error"
severity as blocking, "warning" as worth reviewing but not necessarily
fatal.

## 20. Example
```gdscript
var resource: MotionMatchingResource = load("res://character_a.tres")
for issue in resource.validate():
    if issue["severity"] == "error":
        push_error(issue["message"])
    else:
        push_warning(issue["message"])
```

## 21. Related Classes
`MotionMatchingController` (its consumer), `MotionMatchingDatabase`,
`MMFeatureSchema`, `MMCostFunction` (its contents).
