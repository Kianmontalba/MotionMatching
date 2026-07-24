# MOTION MATCHING — Optimization Guide

Motion matching's cost is dominated by two things: how big the database is,
and how often you search it. This addon addresses both; this doc explains
the knobs.

## Search cost

**KD-tree pruning.** `MMPoseSearch` builds a KD-tree over the normalized
feature vectors at database-finalize time. A weighted nearest-neighbor
descent prunes branches whose bounding box cannot beat the current best cost,
so search time grows sub-linearly with database size rather than linearly.

**Temporal coherence.** Before doing a full tree search, the search checks
whether continuing the current clip (or one of the next few frames) is
already close to optimal. Locomotion rarely needs to jump clips every frame,
so this alone eliminates most full searches during steady-state movement.

**Async worker.** `MMSearchWorker` runs on a dedicated thread with a
single-slot query/response queue: the controller submits a query and reads
back the most recent result on a later frame. If the game thread submits
faster than the worker can search, older queries are simply dropped — the
worker never falls behind or queues up stale work.

**Search budget.** Set a per-frame time budget on the controller; if the
worker cannot keep up (crowd scenarios, low-end mobile), `MMProfiler` counts
budget overruns so you can see it happening rather than silently drop frames.

## LOD (quality levels)

`MMQuality` (`ULTRA`/`HIGH`/`MEDIUM`/`LOW`) selects a *prefix* of the feature
vector to compare, because the schema layout is deliberately ordered cheapest
first:

```
[trajectory position][trajectory direction][root velocity]  <- LOW
[bone positions]                                             <- MEDIUM
[bone velocities]                                             <- HIGH
[extra]                                                        <- ULTRA
```

Lowering quality is therefore a smaller loop bound on the same data — no
separate low-poly database, no extra memory.

Use lower quality for background/crowd characters and full quality for the
player and any character the camera is close to.

## Cache

`MMSearchCache` quantizes the query (position/velocity snapped to a grid)
together with the active filter (required/blocked/any tags, category mask)
into a key, and stores the last result for that key. A character holding
still, or several crowd members in similar states under the same filter, can
hit the cache instead of touching the tree at all. Mixing the filter into
the key is deliberate: it means a change in gameplay state (a jump request,
a category lock, a detected traversal obstacle) always misses even if the
query itself repeats, so the cache can never serve a stale answer that
violates the character's current constraints.

Cache size (1024 entries) and the quantization step (0.15, roughly a
seventh of a standard deviation in the normalized feature space) are
currently fixed constants rather than resource-exposed properties. If a
project needs a different trade-off between hit rate and match precision,
those constants live in `MotionMatchingController::_make_cache_key()` and
the constructor's `_cache.resize()` call.

## Memory

**Flat normalized arrays.** The database stores one big `float` array (all
frames concatenated) rather than per-frame objects, so there is no per-frame
allocation overhead and the search loop is a linear memory scan when the
tree isn't pruning aggressively (small databases, or worst-case queries).

**Parallel metadata arrays.** Per-frame metadata (animation id, tags,
contact flags) is stored in its own packed array rather than interleaved
with feature floats, so the hot loop's cache lines contain only the numbers
being compared.

**Per-group normalization.** Z-score normalization (subtract mean, divide by
standard deviation) is computed once at `finalize()` and baked into the
stored floats, so no per-query normalization pass is needed.

## Build-time performance

**Two-pass sampling.** `MMFeatureExtractor` samples each clip once to
compute normalization statistics and once to write final features, rather
than re-sampling per query at runtime.

**Multi-threading.** `MMAnimationLibraryTools` and the database build loop
are structured to allow parallelizing across clips (each clip's sampling and
statistics are independent) — building a 500-clip library does not need to
be single-threaded, since no clip's output depends on another's.

## Mobile-specific advice

- Prefer `MM_QUALITY_MEDIUM` or `MM_QUALITY_LOW` as the default and reserve
  `HIGH`/`ULTRA` for a "quality" settings toggle.
- Keep the pose-bone list short (hips + both feet is often enough; only add
  hands if upper-body matching matters for your game).
- Trim trajectory sample count — four points (0.2s/0.4s/0.6s/1.0s) is a
  reasonable default; fewer points is a smaller, faster feature vector.
- Use tag/category filtering aggressively before the tree search runs —
  filtering is nearly free and shrinks the effective search space.
- Watch `MMProfiler`'s switch-rate counter: if the character is switching
  clips more often than necessary, raise the hysteresis / minimum-improvement
  threshold on the cost function rather than lowering search quality.
