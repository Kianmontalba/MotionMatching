# MOTION MATCHING — Editor Tools Guide

Covers the 5 `TOOLS_ENABLED`-only classes. These are not present in
exported games — they exist purely to support the authoring workflow
described in `ARCHITECTURE_AND_WORKFLOW.md`'s Part B. Evidence labels as
in the preceding documents.

**Honesty note up front [INFERENCE]:** this guide documents each panel's
purpose and role in the workflow accurately, sourced from each class's own
header-level doc comment. A full button-by-button, slider-by-slider
enumeration would require re-reading every editor `.cpp` implementation
file line by line, which was not repeated in this documentation pass
beyond what earlier sessions already covered. Where a specific control's
exact behavior isn't independently confirmed here, this guide says so
rather than inventing plausible-sounding detail.

---

## MotionMatchingEditorPlugin
**Purpose:** the entry point — adds one bottom panel to the Godot editor
hosting every other tool below, "so the whole workflow lives in one place
instead of across four inspectors" **[SOURCE, its own doc comment]**.
**Workflow:** activates automatically once the addon is enabled; no
per-project setup beyond having the GDExtension loaded.
**Generated assets:** none directly — it's a container for the tools that
do generate assets.

## MMDatabaseEditor
**Purpose:** build, inspect, and save a motion database.
**Overview:** the clip table is the important part of this panel — with
a large clip count (the panel's own doc comment gives "800 clips" as the
scale it's designed for), reviewing and tagging clips one resource at a
time in the standard inspector "is not a workflow" **[SOURCE]**.
**Workflow (matches the stage sequence in
`ARCHITECTURE_AND_WORKFLOW.md`):**
1. Point the panel at a `Skeleton3D` and an `AnimationLibrary`.
2. Scan — runs rig detection and clip analysis, populating a reviewable
   table of detected tags/category per clip.
3. Review/edit the suggested tags and category per clip as needed.
4. Build database — runs the full feature extraction pipeline.
5. Save — writes the resulting `MotionMatchingDatabase` to disk.
**Generated assets:** a `MotionMatchingDatabase` `.tres`/`.res` file.
**Best practices:** always review the Scan step's suggestions before
Build — this is explicitly a reviewable intermediate step in the
underlying `MMFeatureExtractor`/`MMClipAnalyzer` design, not a black box.

## MMFeatureEditor
**Purpose:** edits `MMFeatureSchema` layout and `MMCostFunction` weights.
**Overview:** weight sliders are normalized to a percentage of total cost
so the numbers "mean what a designer expects" **[SOURCE]** — i.e., moving
a slider communicates relative emphasis between feature groups
(trajectory position/direction, pose position/velocity, root velocity,
extra), not an unbounded raw weight value.
**Workflow:** adjust schema settings (trajectory sample times, tracked
bones, optional feature toggles) and cost-function group weights while
observing search behavior, typically alongside `MMDebugTools` for live
feedback.
**Best practices:** re-run `MMCostFunction.rebuild(schema)` (or trigger it
through this panel, which is expected to do so automatically
**[EXPECTED — not independently re-verified against the panel's exact
`.cpp` behavior this pass]**) after any schema change, since a stale
weight table silently describes the wrong layout.

## MMTrajectoryEditor
**Purpose:** tunes `MMTrajectory`'s predictor parameters (position/direction
halflife, prediction step, history duration/interval, max speed) and
explains what each one does to the character's felt responsiveness —
described as "the parameter set most likely to be tuned by someone who
did not write the code" **[SOURCE, its own doc comment]**.
**Workflow:** adjust halflife values while testing character movement
(in the editor's play mode or a live debug session) to feel the
tradeoff between responsiveness (short halflife) and smoothness (long
halflife).
**Best practices:** tune `halflife_position`/`halflife_direction` together
with the animation library's own turn/stop clips in mind — a predictor
that reacts faster than any clip can visually follow will still be
bottlenecked by the clip library.

## MMDebugTools
**Purpose:** a live profiler readout for a selected controller while the
game is actually running (via the editor's remote-debugging connection to
a running game instance **[EXPECTED, standard Godot remote-debugging
mechanism]**).
**Overview:** surfaces `MotionMatchingController.get_debug_info()`'s
contents in a readable panel — matched clip, playback time, blend weight,
match cost, cache hit/miss counts, and (as of this session's controller
integration) the profiler report and the new phase-timing keys, **though
whether this panel's own `.cpp` implementation has been updated to
display the newest keys (profiler report, traversal/warp state) was not
independently re-verified this pass** — treat the underlying
`get_debug_info()` dictionary in `CLASS_REFERENCE_01.md` as the
authoritative list of what's available, and this panel as a display
surface for a subset of it that may lag behind the newest additions.
**Best practices:** use this panel to confirm a character is matching
sensibly before chasing what might just be a bad tag or a stale schema
pairing — cross-reference with `MotionMatchingResource.validate()` if
something looks wrong.

---

## Summary table

| Tool | Primary output | Typical use point in the workflow |
|---|---|---|
| `MotionMatchingEditorPlugin` | (none — hosts the others) | Always active once the addon is enabled |
| `MMDatabaseEditor` | `MotionMatchingDatabase` | Stages 4-7 (Clip Analysis through KD-Tree Build) |
| `MMFeatureEditor` | Edited `MMFeatureSchema`/`MMCostFunction` | Stage 5 (Feature Extraction) setup, and ongoing tuning |
| `MMTrajectoryEditor` | Edited `MMTrajectory` parameters | Stage 9 (Query Building) tuning |
| `MMDebugTools` | Live readout, no saved asset | Ongoing, during runtime testing |
