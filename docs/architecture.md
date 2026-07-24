# MOTION MATCHING — Architecture

## Overview

MOTION MATCHING is a C++ GDExtension for Godot 4.x implementing a full
motion-matching animation system: given a character's current motion intent
(desired velocity, facing, ground state), it searches a precomputed database
of animation frames for the pose that best continues that motion, and plays
from that exact point — no state machines, no manually authored blend trees
for locomotion.

The system is organized in five layers. Each layer only depends on the
layers below it, which is what keeps the codebase modular:

```
┌─────────────────────────────────────────────────────────┐
│  Runtime Layer                                           │
│  MotionMatchingController, AnimationNodeMotionMatching    │
├─────────────────────────────────────────────────────────┤
│  Motion Layer                                             │
│  MMTrajectory, MMRootMotion, MMTraversal, MMMotionWarp     │
├─────────────────────────────────────────────────────────┤
│  Search Layer                                              │
│  MMPoseSearch, MMCostFunction, MMSearchCache               │
├─────────────────────────────────────────────────────────┤
│  Build Layer                                                │
│  MMFeatureExtractor, MMSkeletonProfile, MMClipAnalyzer       │
├─────────────────────────────────────────────────────────┤
│  Data Layer                                                  │
│  MMFeatureSchema, MotionMatchingDatabase, MMAnimationEntry    │
└─────────────────────────────────────────────────────────┘
```

## Data Layer

**MMFeatureSchema** describes what a feature vector contains: how many
trajectory sample points, which bones are tracked, whether bone/root
velocity is included. All other layers read their memory layout from this
one resource, so adding a feature is a schema change, not a search-code
change.

**MotionMatchingDatabase** is a flat, normalized `float` array — one row per
frame, laid out so a search is a single cache-friendly scan (or KD-tree
descent; see Search Layer). Per-frame metadata (animation id, time, root
velocity, tags, category, foot contact flags) lives in parallel packed
arrays, not interleaved with the feature floats, so the hot search loop
touches only the numbers it needs.

**MMAnimationEntry** stores per-clip metadata: source animation path, tags,
category, loop flag, and length — the things that don't vary frame to frame.

## Build Layer — Universal Rig Support

This is the layer that makes the framework animation-pack-agnostic. See
`ARCHITECTURE_DECISIONS.md` for the full rationale; in short:

- **MMSkeletonProfile** detects bone roles (hips, spine, head, both legs,
  both arms) from any `Skeleton3D` using structural analysis first, name
  matching second, and manual override third. No naming convention is
  assumed anywhere else in the codebase.
- **MMClipAnalyzer** classifies clips by measuring their motion (speed, turn
  rate, foot contact, airtime) rather than parsing their name. A name-rule
  table exists but is optional and only supplies tags that motion truly
  cannot reveal (weapon type, attack phase).
- **MMFeatureExtractor** ties the two together: it samples every clip in an
  `AnimationLibrary` against the detected skeleton, measures per-frame pose
  and per-clip statistics, and appends the results into a
  `MotionMatchingDatabase`.

## Search Layer

**MMCostFunction** turns per-group weights (trajectory, pose, velocity) into
a per-dimension weight table once, so the hot path is a single weighted
sum-of-squares with no branching per dimension.

**MMPoseSearch** holds a KD-tree over the database (widest-axis splits,
quickselect median construction) and descends it with weighted pruning. A
temporal-coherence pass also checks the few frames immediately following the
currently playing frame, since continuing the current clip is free of
blending cost and often already near-optimal.

**MMSearchCache** quantizes a query into a cache key and stores recent
results; a repeated query (character standing still, e.g.) is a cache hit.

**MMSearchWorker** runs the search on a background thread with a
single-slot overwrite queue: a controller submits a query and reads back
whatever the worker last finished, never blocking the game thread and never
building up a backlog of stale queries.

## Motion Layer

**MMTrajectory** predicts where the character is going using a
critically-damped spring model driven by desired velocity, and keeps a
short history of where it has been — both are written into the query
because a good match considers future intent and recent motion, not just
the current pose.

**MMRootMotion** integrates root displacement from velocity rather than
diffing absolute transforms, which keeps motion continuous across a frame
jump (the defining move of motion matching — playback can jump from one
clip's frame 40 to a different clip's frame 12 without a pop, because
velocity is continuous even though pose index is not).

**MMTraversal** raycasts along the predicted trajectory to classify
obstacles (step, low vault, high vault, mantle, climb, slide-under) so the
controller can bias the search toward the right category before running it.

**MMMotionWarp** adjusts root motion and pose toward a designer-specified
target (a ledge, a cover point) using windowed warping and a
distance-matching curve, so a vault animation authored for one distance
still lands correctly at another.

## Runtime Layer

**MotionMatchingController** is the `Node` a character script talks to. It
exposes an intent API (`set_desired_velocity`, `set_facing`,
`set_ground_state`, `request_jump`, ...), applies gameplay tag/category
locking, runs hysteresis so it doesn't switch clips for a marginal
improvement, and blends across the switch.

**AnimationNodeMotionMatching** is an `AnimationRootNode` that blends two
clips at whatever absolute time the controller supplies — this is what
makes true frame-seeking possible inside Godot's `AnimationTree` rather than
requiring every clip to restart from time zero.

## Editor Layer

Four tool panels, all `#ifdef TOOLS_ENABLED` and registered only at
`MODULE_INITIALIZATION_LEVEL_EDITOR`: a database builder/validator, a
feature-schema editor with live cost-weight sliders, a trajectory-tuning
panel with a numeric stop-response preview, and a live debug readout that
polls a running controller.

## Design Principles

1. **No naming assumptions.** Every bone name and clip classification is
   discovered, never hardcoded. See `ARCHITECTURE_DECISIONS.md`.
2. **One source of truth per concern.** Rig detection lives only in
   `MMSkeletonProfile`; clip classification lives only in `MMClipAnalyzer`.
   No parallel systems.
3. **Data-driven layout.** The feature vector's shape lives in
   `MMFeatureSchema`, not scattered across the search and extraction code.
4. **Continuity over correctness-per-frame.** Root motion and blending
   prioritize a smooth trajectory over an exact pose match on any single
   frame, because pops are more noticeable than small positional error.
5. **The game thread never blocks on search.** Async search plus a
   single-slot cache keeps the controller's per-frame cost constant
   regardless of database size.
