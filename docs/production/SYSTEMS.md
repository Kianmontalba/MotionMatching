# MOTION MATCHING — Production Systems Documentation

All 20 requested systems. Evidence labels as in the Class Reference parts.
Class-level property/method detail lives in `CLASS_REFERENCE_01–04.md`;
this document is about how each system *behaves* — its architecture,
algorithms, data flow, and operating model.

---

## Motion Matching (the overall technique)
**Purpose:** replace hand-authored animation state machines with a
per-tick nearest-neighbor search over a precomputed frame database.
**Responsibilities:** own the search loop's timing/hysteresis, decide
when a clip switch is worth making, coordinate every other system below.
**Architecture:** a single orchestrator (`MotionMatchingController`)
owning or referencing every other system.
**Internal workflow / Runtime workflow / Data flow:** see
`CLASS_REFERENCE_01.md`'s `MotionMatchingController` entry, Section 3.
**Inputs:** character intent (desired velocity/facing) each tick.
**Outputs:** a root-motion `Transform3D` delta, a matched (clip, time)
fed to `AnimationNodeMotionMatching`.
**Dependencies:** every system listed below.
**Algorithms:** nearest-neighbor search (KD-tree, see below), hysteresis
(minimum-improvement + cooldown-gated switching — the exact hysteresis
constants live on `MotionMatchingResource`).
**Thread model:** main thread orchestrates; one optional background
thread performs the search itself (see Async Search Worker).
**Memory model:** the controller owns its subsystems by value or `Ref<>`;
the database/schema/cost-function are `Ref<>`-shared from the assigned
`MotionMatchingResource`, not copied.
**Performance characteristics:** dominated by search frequency
(`search_interval`) and whether a given tick's search hits the cache.
**Limitations:** a query with no close match in the database still
returns *something* — the least-bad frame — it does not fail gracefully
by itself; pre-flight validation (`MotionMatchingResource.validate()`,
`MMAnimationLibraryTools.validate_library()`) is the intended safeguard
against building a database with poor coverage in the first place.
**Best practices:** validate the database before shipping; tune
`search_interval` and hysteresis constants for your target frame budget
rather than searching every tick unconditionally.

---

## Pose Search
**Purpose:** the actual nearest-neighbor operation, using `MMCostFunction`
as the distance metric.
**Architecture:** `MMPoseSearch` wraps both a KD-tree implementation and a
brute-force linear-scan reference implementation.
**Internal workflow:** descend the tree, pruning branches whose bounding
region's distance already exceeds the best cost found so far (aided by a
seed cost — typically "the cost of continuing the current clip" — handed
in via `MMSearchContext`).
**Data flow:** query vector + filter + context → tree descent → best
match + stats.
**Algorithms:** branch-and-bound nearest-neighbor over a KD-tree; the
brute-force path is a plain linear scan used only to verify the tree
agrees with it (`test_kdtree_vs_bruteforce.gd`).
**Thread model:** stateless-with-respect-to-search — the tree is
read-only once built, so `search()` can safely run on the background
worker thread.
**Memory model:** the tree itself owns index arrays into the database's
existing flat feature block — it does not duplicate feature data.
**Performance:** expected to be substantially faster than brute-force on
any non-trivial database (that's the entire reason it exists); no
comparative timing has actually been measured in this environment.
**Limitations:** filter-based exclusion (tags/category) happens *during*
the walk, not as a post-filter step — efficient, but an overly
restrictive filter can still require walking a large portion of the tree
before failing to find anything.

---

## KD-Tree
**Purpose:** the acceleration structure itself, underlying Pose Search.
**Architecture:** binary space partition over the feature vector's
dimensions.
**Algorithm:** splits on the *widest* axis, estimated from a bounded
sample rather than the full dataset, keeping build time close to linear
even for large databases; leaf size is configurable (`leaf_size`
property).
**Data flow:** built once from a finalized `MotionMatchingDatabase`;
queried many times per second thereafter.
**Memory model:** built as an array of nodes plus an index array into the
database's frame data — confirmed via source inspection of
`_build_recursive()`'s approach; no per-frame data is copied into the
tree structure itself.
**Thread model:** immutable once built; this immutability is exactly
what lets the background search worker read it without a mutex.
**Performance:** rebuild cost is not incremental — adding one clip means
rebuilding the whole tree from scratch. This is acceptable for an
authoring-time build step, not for runtime streaming of new content.
**Limitations:** not designed for incremental updates; not designed for
distributed/multi-database search (one controller, one tree).

---

## Search Cache
**Purpose:** skip the tree walk entirely for a repeated (or
near-repeated) query under the same gameplay-state filter.
**Architecture:** direct-mapped — one slot per hash bucket; a collision
simply evicts the previous occupant rather than chaining.
**Algorithm:** key = a quantized hash of the query, mixed with the active
search filter's tag/category bits via a hash-combine-style function. The
filter is mixed in specifically so a gameplay-state change (jump lock,
traversal bias) can never reuse a cached answer that violates the *new*
filter — this was the single correctness-critical design decision in
wiring this system up.
**Data flow:** query+filter → key → lookup (hit: reconstruct match from
stored frame+cost; miss: fall through to a real search, then store the
result under the same key).
**Thread model:** **not thread-safe itself** — correctness depends
entirely on only ever being touched from the main thread, inside
`_run_search()`/`_consume_result()`. It is never accessed from the
background search worker thread.
**Memory model:** fixed-size (1024 entries), allocated once; no dynamic
growth.
**Performance:** a hit is O(1) versus the tree's O(log n)-ish descent —
the entire performance benefit of this system.
**Limitations:** size (1024) and quantization step (0.15) are fixed
internal constants, not resource-tunable.
**Best practices:** if you observe a low hit rate for a specific
project's movement style, this indicates the fixed quantization step
doesn't suit your feature-vector scale — currently the only remedy is
editing the constant in source, not a runtime setting.

---

## Async Search Worker
**Purpose:** runs the tree search on a background thread so the game
thread never blocks on a descent.
**Architecture:** exactly one thread, single-slot request/response
handoff — a deeper queue would only add latency, since by the time an
old request finished the character would already want something
different.
**Thread model (reviewed in detail this session):** `submit()`/`poll()`
lock a `std::mutex` around the shared request/response slots; the
worker's wait predicate correctly wakes on either a new request or a
shutdown signal, so `stop()` cannot deadlock waiting on a stuck thread.
The database and tree are read without a lock because they are immutable
once built, and access is serialized by `rebuild()`'s
stop-before-rebuild ordering rather than by a mutex. A raw
`MMCostFunction*` pointer is temporarily promoted to an owning `Ref`
inside the worker's search call specifically to survive a concurrent
main-thread `Ref` drop — this pattern was reviewed and found correct,
relying on `RefCounted`'s atomic refcount.
**Memory model:** the worker owns its own copy of the query vector
(copied in `submit()`, under the lock) — it does not read the
controller's live query buffer, avoiding a data race on that specific
piece of data.
**Verified defect, fixed this project:** `MotionMatchingController::set_resource()`
previously reassigned resource-owned `Ref`s *before* stopping this
worker, so a search in flight could be left holding a dangling raw
pointer if the reassignment destroyed the object it pointed to. Fixed by
reordering `set_resource()` to stop the worker first.
**Performance:** moves tree-descent cost off the main thread entirely, at
the cost of exactly one tick of result latency (the response is polled,
not awaited).
**Limitations:** exactly one worker thread total per controller — no
parallelization of a single search across multiple threads, only
offloading of the whole search to one other thread.

---

## Profiler
**Purpose:** search-time percentiles, clip-switch-rate, and
budget-overrun counting.
**Architecture:** a ring-buffer-style sample collector (`MMProfiler`),
always instantiated by the controller (unlike the opt-in traversal/warp
subsystems).
**Data flow:** fed by `record()` (every real, non-cached search) and
`record_switch()` (every accepted clip switch) → `get_report()` →
`get_debug_info()["profiler"]`.
**Thread model:** fed only from the main thread — both the synchronous
search path and the async-response-consumption path that calls
`record()` run on the main thread; the background worker itself never
calls into the profiler directly.
**Memory model:** fixed-size internal sample storage (not independently
re-verified this pass — treat capacity as an implementation detail, not
a documented tunable).
**Performance:** designed to be cheap enough to run unconditionally
(no sampling/toggle needed) — recording one sample is O(1).
**Limitations:** does not track the 4 additional phase timings (query
build, continuation eval, switch apply, total update) added alongside it
this session; those live as separate controller members instead, since
they don't fit this class's search-shaped internal sample structure.

---

## Root Motion
**Purpose:** integrates character displacement from *velocity*, never
from the difference between two absolute root transforms.
**Architecture:** a single accumulator (`MMRootMotion`) that integrates
smoothed velocity every tick and hands off the accumulated transform on
demand.
**Algorithm:** exponential smoothing (halflife-based) of the target
velocity, with a special case for a motion-matching frame jump: the tick
immediately following `notify_frame_jump()` now adopts the new frame's
velocity immediately rather than easing in — this exact mechanism was a
verified, fixed defect this session (previously dead code: the adoption
fields were written but never read).
**Data flow:** database per-frame velocity → blend (if mid-crossfade) →
smoothing → integration → accumulated transform → `consume_delta()`.
**Thread model:** main-thread only.
**Memory model:** a single `Transform3D` accumulator plus a handful of
scalar/vector smoothing-state fields — no per-frame allocation.
**Performance:** O(1) per tick regardless of database size.
**Limitations:** the frame-jump fix has never been executed/observed —
verified by source inspection and an authored-but-unexecuted GDScript
test, not by an actual run.
**Best practices:** call `consume_delta()` exactly once per tick you
intend to apply it.

---

## Motion Warp
**Purpose:** bends a clip's root motion toward a gameplay-determined
target it wasn't authored for.
**Architecture:** a window-based correction system (`MMMotionWarp`) —
one or more time windows, each with a start/end time and a target
transform; the correction is spread across the window's duration and
applied to the root *delta*, never distorting the sampled pose.
**Data flow:** `begin()` + `add_window()` → per-tick `warp_delta()`
correction, consulted by the controller's `consume_root_motion()` only
while `is_active()`.
**Integration point:** controller-level `begin_warp()`/`end_warp()`
convenience methods, added this session, opening one default window
spanning current time → end of the currently-playing clip.
**Thread model:** main-thread only, called synchronously inside
`consume_root_motion()`.
**Performance:** O(number of active windows) per tick — expected to be
small in practice.
**Limitations (explicitly unsolved, stated in source):** no automatic
handling if a clip switch happens mid-warp — old windows authored for the
previous clip's timeline would misapply against the new clip's playback
time. This is documented caller responsibility, not solved by the addon.

---

## Traversal
**Purpose:** raycasts along the predicted trajectory to detect and
classify an approaching obstacle.
**Architecture:** an opt-in `MMTraversal` instance; zero cost unless
explicitly assigned to a controller.
**Algorithm:** raycast-based classification along the trajectory's
predicted path, producing a `TraversalType` plus entry/top/exit points
and a required-tags bitmask.
**Data flow:** trajectory + physics world → probe → detected type/points
→ controller's `traversal_requested` signal + search-filter bias (via
`get_required_tags()`).
**Runtime workflow:** probed once per controller tick, only when
grounded and a character node resolves.
**Thread model:** main-thread only — Godot physics queries are not
generally safe from arbitrary background threads.
**Performance:** one or more real physics raycasts per probe — not free,
which is why probing is gated to grounded-only and fully opt-in.
**Limitations:** integrated into the controller this session but never
executed against real physics geometry — logically wired, not
runtime-confirmed.

---

## Feature Extraction
**Purpose:** converts an `AnimationLibrary` + `Skeleton3D` into a
searchable `MotionMatchingDatabase`.
**Architecture:** `MMFeatureExtractor` orchestrates `MMPoseSampler`
(per-clip sampling), `MMSkeletonProfile` (rig), `MMClipAnalyzer`
(classification), and `MMFeatureSchema` (layout) — no pack-specific logic
lives in this system.
**Runtime workflow:** build/editor-time only.
**Data flow:** see the Workflow document's "Database Generation
Pipeline" section for the full stage sequence.
**Algorithms:** per-clip sampling at a configurable rate; foot-contact
detection via configurable speed/height ratio thresholds (hip-height
normalized).
**Thread model:** single-threaded — the per-clip build loop is
"structured to allow" parallelizing (independent clips, independent
output) but does not currently do so.
**Memory model:** builds the database's flat packed arrays incrementally,
one clip's frames appended at a time.
**Performance:** cost scales with total sampled frames (clip count ×
average length × sample rate) across the whole library.
**Limitations:** single-threaded build loop (see Thread model).

---

## Feature Schema
**Purpose:** the single description of feature-vector layout every other
system reads from.
**Architecture:** `MMFeatureSchema` computes and caches offsets/dimension
whenever a relevant setting changes.
**Algorithm/design decision:** feature order is a deliberate prefix
structure — trajectory positions, trajectory directions, root velocity
(optional), bone positions, bone velocities (optional), extra dimensions
(optional), in that order — so every quality/LOD level is a prefix of the
full vector, meaning a cheaper search is the *same loop* with a smaller
dimension bound, not a different code path.
**Data flow:** skeleton → `apply_skeleton_profile()` → layout → consumed
by extractor, cost function, database, and runtime query building alike.
**Memory model:** the layout itself is small metadata (offsets, a
dimension count, a small per-dimension group table) — not proportional to
database size.
**Limitations:** changing the schema after a database is built requires
rebuilding the database; `MotionMatchingResource.validate()` is the
mechanism that catches a stale pairing before it silently produces wrong
search results.

---

## Animation Database
See `MotionMatchingDatabase` in `CLASS_REFERENCE_02.md` for full class
detail. **Architecture summary:** flat packed arrays (feature block,
per-frame animation id/time/velocity/contact data), immutable once
`finalize()`d. **Memory model:** allocated once at build time, sized to
`frame_count × dimension` for the feature block plus smaller per-frame
scalar/vector arrays — no further allocation at runtime. **Thread model:**
safe to read concurrently from multiple threads once finalized, since
nothing mutates it afterward (this is exactly what permits the background
search worker to read it lock-free).

---

## Cost Function
See `MMCostFunction` in `CLASS_REFERENCE_02.md`. **Architecture summary:**
a flat per-dimension weight table, baked from per-group weights via
`rebuild(schema)`. **Algorithm:** branchless weighted squared distance
with an early-out bound. **Performance:** the early-out typically skips
60-80% of dimension comparisons per its own doc comment (a stated design
rationale, not an independently measured benchmark). **Thread model:**
stateless per call aside from the read-only weight table — safe to call
from the background search thread.

---

## Rig Detection / Skeleton Profile
(These are the same system — both names refer to `MMSkeletonProfile`; see
`CLASS_REFERENCE_03.md` for full detail.) **Architecture:** three-pass
detection — structural graph analysis, normalized name-token matching,
manual override — strongest evidence first. **Algorithm:** leg chains
identified via deepest-leaf/chain-length graph analysis from a shared
ancestor; head/arms identified by chain shape; left/right disambiguated
by name tokens recognized across every common convention (Mixamo, UE,
Rokoko, Blender, generic mocap). **Memory model:** small metadata (bone
name strings per role, a locked-roles bitmask) — not proportional to
animation data size. **Limitations:** requires a minimum bone count to
attempt biped detection at all.

---

## Clip Analysis
See `MMClipAnalyzer` in `CLASS_REFERENCE_03.md`. **Architecture:**
threshold-based classification against measured `MMClipStats`, all
thresholds normalized by hip height. **Algorithm:**
`calibrate_speed_bands()` derives thresholds from a specific library's
own speed distribution (quartiles of moving clips) rather than trusting
fixed defaults to fit an unfamiliar pack. **Limitations:** the default
thresholds are reasonable starting points, not guaranteed-correct for
every creature scale/style — calibration is recommended, not optional,
for anything unusual.

---

## Foot IK
See `MMFootIKModifier` in `CLASS_REFERENCE_04.md`. **Architecture:**
analytic two-bone solve per leg (via `MMIKSolver`) plus a pelvis-height
adjustment, driven by raycasts and database-baked contact flags.
**Data flow:** database contact bits → controller debug info → gameplay
script → `set_foot_contacts()` → lock state → solve. **Performance:**
2 raycasts/tick, constant-time analytic solve. **Limitations:** requires
correctly-configured bone names; a mismatch is now warned about once
(fixed this session's predecessor work) rather than failing silently.

---

## Aim IK
See `MMAimIKModifier` in `CLASS_REFERENCE_04.md`. **Architecture:**
weighted multi-bone chain rotation toward a smoothed target, with a
cone-angle limit. **Runs after Warp Modifier** in the intended pipeline
order, so it aims from the already orientation-corrected pose.

---

## Warp Modifier
See `MMWarpModifier` in `CLASS_REFERENCE_04.md`. **Architecture:**
pose-level (not root-motion-level) warping — orientation, stride, and
lean — applied after the animation is sampled. **Distinct from "Motion
Warp"** (which operates on the root delta before application) — the two
systems are complementary, not overlapping.

---

## AnimationTree Integration
See `AnimationNodeMotionMatching` in `CLASS_REFERENCE_04.md`.
**Architecture:** a custom `AnimationRootNode` that blends exactly two
clips using `blend_animation()`'s absolute-time argument — the specific
Godot API capability that allows starting mid-clip, which is the whole
mechanical basis motion matching depends on to avoid restarting clips
from frame zero.
