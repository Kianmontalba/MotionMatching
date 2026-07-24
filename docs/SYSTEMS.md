# MOTION MATCHING — Systems Documentation

Each system below: Purpose / Internal workflow / Data flow / Inputs /
Outputs / Important algorithms / Dependencies / Limitations. All
**[SOURCE]** (read directly from the implementation) unless marked
**[INFERENCE]**. Class-level detail (properties, methods) lives in
`CLASS_REFERENCE.md`; this file is about how the systems *behave together*.

---

### Motion Matching (the technique, as implemented here)
**Purpose:** replace hand-authored animation state machines with a search:
every tick, build a query describing "what the character wants to do next,"
find the closest-matching frame across every clip in a database, and play
from there — including mid-clip, never restarting at frame 0.
**Data flow:** character intent → `MMTrajectory` → query vector →
`MMPoseSearch` (cache-checked first) → `MMMatchResult` → hysteresis check
(`_should_switch()`) → `_apply_match()` → `MMRootMotion`/`AnimationNodeMotionMatching`.
**Limitations:** quality is bounded by database coverage — a query with no
close match still returns *something* (the least-bad frame), it doesn't
fail gracefully by itself; `MMAnimationLibraryTools::validate_library()`
and `MotionMatchingResource::validate()` are the pre-flight checks meant to
catch coverage/setup problems before this becomes a runtime surprise.

### Pose Search
**Purpose:** the actual nearest-neighbor search over the database's feature
vectors, using `MMCostFunction` as the distance metric.
**Internal workflow:** `MMPoseSearch::search()` descends the KD-tree,
pruning branches whose bounding-box distance already exceeds the best cost
found so far (aided by `context.best_cost_seed`, typically the cost of
simply continuing the current clip). `search_brute_force()` is the linear
reference implementation used only for correctness testing (now GDScript-
callable via `search_brute_force_query()`, added this session).
**Inputs:** a query vector, a `MMSearchFilter` (required/blocked/any tags,
category mask), a `MMSearchContext` (best-cost seed).
**Outputs:** an `MMMatchResult` (frame index, animation id, time,
normalized time, cost) plus `MMSearchStats` (frames compared, candidates
visited, nodes visited, time, cache hit/miss counts).
**Limitations:** filter-based exclusion (tags/category) happens *during*
the tree walk, not as a post-filter — this is efficient but means an
overly restrictive filter can cause the whole tree to be walked with
nothing found.

### KD-Tree Search
**Purpose:** the acceleration structure itself.
**Important algorithm:** splits on the *widest* axis (estimated from a
bounded sample, not the full dataset) rather than cycling dimensions in a
fixed order, which keeps build time roughly linear even for very large
databases while still producing good splits.
**Dependencies:** `MMCostFunction`'s per-dimension weight table, needed for
correct pruning (a branch can only be skipped once `weight[dim] * delta² `
already exceeds the current best).
**Limitations:** rebuilding the tree (`rebuild()`) is not incremental —
adding one clip means rebuilding the whole structure; this is acceptable
for an authoring-time database build, not for incremental runtime
streaming (not a use case this addon targets).

### Search Cache
**Purpose:** skip the tree walk entirely for a repeated (or
near-repeated, after quantization) query under the same gameplay-state
filter.
**Internal workflow:** direct-mapped, single entry per hash bucket
(collisions simply evict, no chaining). Key = `MMSearchCache::make_key()`
on the quantized query, mixed (this session's addition) with the active
filter's tag/category bits via a boost::hash_combine-style function.
**Why the filter is mixed in [SOURCE]:** without it, a gameplay-state
change (jump, category lock, traversal) could reuse a cached answer that
violates the *new* filter — this was the one correctness-critical decision
in wiring this system up.
**Limitations:** cache size (1024) and quantization step (0.15, roughly a
seventh of a standard deviation in normalized feature space) are fixed
internal constants, not resource-tunable, per `docs/optimization.md`'s
correction this session.

### Search Worker
**Purpose:** runs the search on a background thread so the game thread
never blocks on a tree descent.
**Internal workflow:** single-slot request/response, overwriting the
pending request is the intended policy (not a shortcut) — by the time an
old request finished, the character would already want something else.
**Thread-safety:** genuinely reviewed this session; see
`CLASS_REFERENCE.md`'s `MMSearchWorker` entry for the specific mutex/atomic
reasoning, and `PRE_RELEASE_REVIEW.md` for the one real race this system
was involved in (fixed: `set_resource()` could destroy a `Ref` the worker
still held a raw pointer to).
**Limitations:** exactly one worker thread total per controller — this
addon does not attempt to parallelize a single search across multiple
threads, only to move the whole search off the main thread.

### Profiler
**Purpose:** search-time percentiles, switch-rate, budget-overrun
counting.
**Data flow:** fed by `record()` (real, non-cached searches only) and
`record_switch()` (every accepted clip switch); read via `get_report()`,
now surfaced through `get_debug_info()["profiler"]`.
**Limitations:** never fed by cache hits (deliberate, avoids diluting
percentiles); the 4 additional phase timings this session added (query
build, continuation eval, switch apply, total update) live *outside* this
class, as plain controller members, since they don't fit its
search-shaped `Sample` struct.

### Root Motion
**Purpose:** integrates character displacement from velocity, so a clip
switch never causes a positional pop.
**Important algorithm:** velocity, not absolute-transform-delta, is the
integrated quantity — two unrelated clips' frame transforms don't compare
meaningfully, but their velocities do.
**Verified defect fixed this session:** `notify_frame_jump()`'s intended
"adopt new velocity immediately" behavior was dead code (wrote fields
`update()` never read) — fixed.
**Limitations:** the fix's correctness under real playback has never been
executed, only reasoned from source and checked by an unexecuted GDScript
test (`test_root_motion_continuity.gd`).

### Motion Warp
**Purpose:** bends root motion toward a target the clip wasn't authored
for, applied to the root *delta* (never distorting the sampled pose
itself).
**Data flow:** `begin(start_transform)` → one or more `add_window(start_time,
end_time, target)` → each tick, `warp_delta()` computes how much correction
remains and blends it into the raw root delta → `end()`.
**Integration this session:** controller-level `begin_warp()` opens one
default window spanning current time → end of the currently-playing clip;
finer control is available via `get_motion_warp()->add_window()` directly.
**Limitations:** no automatic handling of a clip switch mid-warp — explicit,
documented caller responsibility, not solved by this addon.

### Traversal
**Purpose:** raycasts along the predicted trajectory to detect and
classify an approaching obstacle.
**Data flow:** `MMTraversal::probe(space_state, character_transform,
trajectory)` → detected type + entry/top/exit points + required tags →
(this session) controller emits `traversal_requested` signal and biases
the next search's filter via `get_required_tags()`.
**Limitations:** only ever probed when grounded and a character node
resolves; opt-in (null by default, zero cost unless assigned). Never
executed against real physics geometry in this environment.

### Feature Extraction
**Purpose:** converts an `AnimationLibrary` into a searchable
`MotionMatchingDatabase`.
**Internal workflow:** `MMPoseSampler` binds each clip to the skeleton
once; per sampled frame, pose bone positions/velocities, root velocity,
and trajectory-shaped history/future (reconstructed from the clip itself
during building) are packed into the schema's layout; `MMClipAnalyzer`
classifies the whole clip from its measured `MMClipStats`.
**Dependencies:** `MMSkeletonProfile` (rig), `MMFeatureSchema` (layout),
`MMClipAnalyzer` (classification).
**Limitations:** the per-clip build loop is single-threaded, despite the
codebase's own documentation noting it's "structured to allow"
parallelizing across clips — not yet done.

### Feature Schema
**Purpose:** the single description of feature-vector layout every other
system reads from.
**Important design decision [SOURCE]:** feature order is a deliberate
prefix structure — every quality/LOD level is a prefix of the full vector,
so a cheaper search is the *same loop* with a smaller dimension bound, not
a different code path.
**Limitations:** changing the schema after a database is built requires
rebuilding the database — `MotionMatchingResource::validate()` (this
session) is the mechanism that catches a stale schema/database pairing
before it causes silently-wrong search results.

### Animation Database
See `MotionMatchingDatabase` in `CLASS_REFERENCE.md`. **Data flow summary:**
built once (offline/editor-time) → `finalize()` locks in dimension and
stamps `format_version` → loaded and handed to `MMPoseSearch::build()` at
runtime → read-only from then on.

### Cost Function
**Purpose:** the distance metric the search optimizes.
**Important algorithm:** per-group weights baked into a flat per-dimension
table; the hot-path `compute_raw()` is a branchless weighted squared
distance with an early-out bound that typically skips 60-80% of the work
per its own comment.
**Limitations:** `switch_penalty` (extra cost for a non-continuation frame)
exists as a property but was flagged in a prior audit as not confirmed to
actually be consulted in `_should_switch()` — this remains unverified, not
re-checked this session.

### Rig Detection
See `MMSkeletonProfile` in `CLASS_REFERENCE.md`. **Important algorithm
summary:** structural graph analysis first (works on anonymously-named
bones), name-token matching second (disambiguates left/right, recognizes
every common naming convention by token), manual override last (always
wins, persists across re-detection).

### Skeleton Profile
Same class as "Rig Detection" above — the two are the same system,
referenced by both names in the original request.

### Clip Analysis
See `MMClipAnalyzer` in `CLASS_REFERENCE.md`. **Important algorithm
summary:** classification from *measured* motion (`MMClipStats`) —
speed/turn-rate/airtime/contact-ratio, all normalized by hip height so the
same thresholds work across creature scales — not from clip names.
`calibrate_speed_bands()` derives thresholds from a specific library's own
speed distribution.

### Foot IK
See `MMFootIKModifier` in `CLASS_REFERENCE.md`. **Data flow:** database
contact bitmask → `MotionMatchingController::get_debug_info()` →
gameplay script → `set_foot_contacts()` → lock state → analytic two-bone
solve to raycast-adjusted ground height.

### Aim IK
See `MMAimIKModifier` in `CLASS_REFERENCE.md`. **Data flow:** external
`target` (Vector3) → halflife-smoothed → distributed across a weighted
bone chain with a cone-angle limit.

### Warp Modifier
See `MMWarpModifier` in `CLASS_REFERENCE.md`. Distinct from "Motion Warp"
above — this operates on the already-*sampled pose* (orientation/stride/lean),
while `MMMotionWarp` operates on the root motion *delta* before it's ever
applied. The two are complementary, not overlapping.

### Animation Node Integration
See `AnimationNodeMotionMatching` in `CLASS_REFERENCE.md`. **Data flow:**
controller decides a match → `AnimationNodeMotionMatching::_process_animation_node()`
blends the outgoing and incoming clip via `blend_animation()` with an
*absolute* time argument, which is the specific Godot `AnimationTree` API
detail that makes starting mid-clip possible at all.
