# MOTION MATCHING — Build Readiness Report

**Date:** 2026-07-24
**Scope:** Priority 1 items from `AUDIT_REPORT.md` only. No new features, no architectural changes, no unrelated refactors.
**Files touched:** 3 (`include/feature_extractor.hpp`, `src/feature_extractor.cpp`, `src/motion_matching.cpp`) — confirmed by mtime diff against the rest of the tree; nothing else was modified.

---

## 1. Compiler errors fixed

### Error 1: undefined member `MMFeatureExtractor::guess_tags_from_name`

**Where it would have failed:**
- `include/animation_library.hpp:32` — `MMAnimationLibraryTools::auto_tag_library()`

**Root cause:** Regression from the earlier rig-detection consolidation (`MMRigProfile`/`MMTagRules` → `MMSkeletonProfile`/`MMClipAnalyzer`). The old wrapper methods that delegated to the now-deleted `MMTagRules` were removed, but nothing was put in their place pointing at `MMClipAnalyzer`, even though two call sites still depended on the static `MMFeatureExtractor::guess_tags_from_name()` entry point.

**Fix:** Restored `MMFeatureExtractor::guess_tags_from_name(const String &p_name)` as a static method (declared in `include/feature_extractor.hpp`, defined in `src/feature_extractor.cpp`). It does not reintroduce a second classification system — it constructs a scratch `MMClipAnalyzer` (the single canonical classifier), disables motion analysis (there is no sampled motion available at this call site — it runs before a skeleton is even chosen, during the editor's Scan step), and enables name rules. With the shipped-empty default name-rule table, this honestly returns `MM_TAG_IDLE` rather than fabricating motion data that was never measured. The real, motion-based tags are still assigned later by `analyze_library()`/`build_database()` once a skeleton is scanned — this method only restores the pre-skeleton preview behavior.

**Why this approach and not another:** The alternative (giving `MMClipAnalyzer` a new default name-rule table to make this method "smarter") was rejected as out of scope — the task explicitly excludes new features and redesigns, and `MMClipAnalyzer` shipping with an empty name-rule table by default is a deliberate design choice from the original build session, not a bug.

### Error 2: undefined member `MMFeatureExtractor::guess_category_from_tags`

**Where it would have failed:**
- `include/animation_library.hpp:35` — `MMAnimationLibraryTools::auto_tag_library()`
- `editor/database_editor.cpp:235` — `MMDatabaseEditor::_on_clip_edited()`

**Root cause:** Same regression as Error 1.

**Fix:** Restored `MMFeatureExtractor::guess_category_from_tags(int p_tags)` as a static method. Unlike Error 1's fix, this one needed no judgment call — it is a pure one-line delegation to `MMClipAnalyzer::category_for_tags(p_tags)`, which already existed and already does exactly this mapping (used internally by `build_database()`'s real classification path). No new logic was written; this restores a name, not a behavior.

**Both methods are also registered with `ClassDB::bind_static_method()`** in `MMFeatureExtractor::_bind_methods()`, matching how every other static utility method in the codebase (e.g. `MMClipAnalyzer::category_for_tags` itself) is exposed to GDScript — kept for API consistency, not because either call site requires it (both call sites are C++, not script).

---

## 2. Linker errors fixed

None found or expected to be introduced by this pass. The two errors above are undefined-symbol *compiler* errors (the methods don't exist at all, so the compiler rejects the call before it ever reaches the linker) — not link-time-only errors (declared-but-undefined, ODR violations, or missing translation units). No other linker-class issue was found during the full-codebase symbol cross-check below.

---

## 3. Dead references / broken includes / stale declarations removed

**None found beyond what the previous consolidation session already removed.** This pass performed a full, mechanical, whole-codebase verification rather than a spot check:

- **Every `Class::method(` call site in `src/*.cpp` and `editor/*.cpp`** was cross-referenced against every method declared in `include/*.hpp` and `editor/*.hpp`. Zero unresolved calls remain (down from the 2 confirmed by the prior audit — both now fixed above; the audit's third-party grep hits in `editor/database_editor.cpp` for unrelated methods were false positives of an incomplete first pass, corrected and re-verified with editor headers included).
- **Every local `#include "..."`** across `include/`, `src/`, and `editor/` was checked against the actual filenames present in those directories. Zero dangling includes.
- **`rig_profile`/`tag_rules`/`MMRigProfile`/`MMTagRules`** — re-searched across `include/`, `src/`, `editor/`, `tests/`, `demo/`. Zero references (the only place these names still appear is `ARCHITECTURE_DECISIONS.md`'s own explanation of why they were removed, which is documentation, not code).

No stale declarations were found to remove — the bug in §1 was a *missing* declaration, not a stale one.

---

## 4. Foot-contact data path (Priority 1, item 2)

**Problem (confirmed by audit):** `MotionMatchingController::get_debug_info()` never exposed foot contact state, so `demo/player.gd`'s `_foot_ik.set_foot_contacts(info.get("left_foot_contact", false), info.get("right_foot_contact", false))` always received `false, false` regardless of what the database actually recorded for the current frame.

**Fix, in `src/motion_matching.cpp`:** `get_debug_info()` now reads `_database->get_frame_contacts_value(_current_frame)` — a method that already existed on `MotionMatchingDatabase` (bit 0 = left foot, bit 1 = right foot, written by the feature extractor at database-build time; nothing new was added to the database or extractor) — decodes the two bits, and adds `info["left_foot_contact"]` / `info["right_foot_contact"]` to the returned dictionary. Guarded with the same bounds check pattern (`_current_frame >= 0 && _current_frame < _database->get_frame_count()`) used elsewhere in the file, so an unbuilt or not-yet-searched controller safely reports `false, false` instead of reading out of bounds.

**Why `get_debug_info()` and not a new dedicated accessor:** The audit noted a dedicated `get_foot_contacts()` accessor would be architecturally cleaner (gameplay-relevant data arguably shouldn't live only in a debug dictionary), but `demo/player.gd` already calls `get_debug_info()` specifically for these two keys, and the task instructions for this pass are to integrate the existing path, not redesign it. The cleaner-accessor idea remains logged as a Priority 3 item in `AUDIT_REPORT.md` and was deliberately not done here.

**Verified downstream, unchanged:** `MMFootIKModifier::set_foot_contacts(bool, bool)` (in `src/ik_system.cpp`) already correctly stores these into `_left_locked`/`_right_locked` and consumes them at line 337 inside `_process_modification()` — this half of the path was already correct per the audit; only the data source feeding it was broken. No changes were needed or made to `ik_system.cpp`.

**End-to-end path, now complete:**
```
MMFeatureExtractor (raycast-sampled at build time)
  → MotionMatchingDatabase._frame_contacts (already existed)
  → MotionMatchingController.get_debug_info()["left_foot_contact"/"right_foot_contact"] (fixed this pass)
  → demo/player.gd (already read these keys correctly)
  → MMFootIKModifier.set_foot_contacts() (already consumed them correctly)
```

---

## 5. Registered-class verification

Re-confirmed (unchanged from the audit, re-checked after this pass's edits since `feature_extractor.hpp`/`.cpp` were touched): every `GDCLASS(...)`-declared class in `include/` and `editor/` (27 total) has exactly one matching `GDREGISTER_CLASS(...)` call in `src/register_types.cpp`, correctly split between `MODULE_INITIALIZATION_LEVEL_SCENE` and the `TOOLS_ENABLED`-gated `MODULE_INITIALIZATION_LEVEL_EDITOR` block. `MMFeatureExtractor` itself was not renamed or restructured by this pass, so its registration entry required no change and was re-verified present.

---

## 6. Compile verification — honest limitation

**This report cannot say "the project now compiles" from a real build, and does not claim to.** Three independent full-budget attempts were made in this session (and two in the prior audit session — five total across both sessions) to build against a freshly cloned `godot-cpp` (4.3 branch) using `scons`. Every attempt was killed by the tool environment's execution time limit before producing a single compiled object file, and — critically — **no state persists between attempts**: each retry starts from zero compiled objects, meaning this sandbox does not sustain a background/long-running build process across tool-call boundaries. This is an environment constraint, not evidence about the code.

**What this report can say, with confidence:** a full, mechanical, whole-codebase static verification found:
- Zero unresolved `Class::method()` call sites (previously 2, both now fixed).
- Zero unresolved local `#include`s.
- Zero stale references to deleted classes.
- Zero registration mismatches.
- The two specific fixes are minimal, targeted, and each traced to a concrete call site with a concrete before/after.

This is strong evidence the project *should* compile, but it is not the same claim as a green build log. The single highest-priority remaining action — ahead of any further feature or polish work — is running an actual build in an environment that can sustain the process (a real CI runner or a local machine), per the roadmap already logged in `AUDIT_REPORT.md`.

---

# Build Readiness Report — Summary

### Remaining compile errors
**None found by static analysis.** Not independently confirmed by a real build (see §6). If a real build surfaces something static analysis couldn't catch — a type mismatch, a const-correctness issue, a template resolution problem — it would not have been visible to the grep/AST-light methods used in this pass, which check symbol *existence* and *resolution*, not full semantic correctness.

### Remaining linker errors
**None found by static analysis.** Same caveat as above — full linker verification requires an actual link step, which could not be completed in this environment.

### Remaining runtime blockers
None newly introduced by this pass. All runtime-integration gaps identified in the audit (`MMSearchCache` never queried, `MMTraversal`/`MMMotionWarp`/`MMProfiler` never wired into the controller) remain exactly as documented in `AUDIT_REPORT.md` — these are Priority 2 items and were correctly left untouched per this task's scope (no new features, no architecture changes).

### Files modified
| File | Change |
|---|---|
| `include/feature_extractor.hpp` | Added two static method declarations: `guess_tags_from_name`, `guess_category_from_tags` |
| `src/feature_extractor.cpp` | Added the two corresponding implementations, plus their `ClassDB::bind_static_method` registration |
| `src/motion_matching.cpp` | `get_debug_info()` now reads and exposes `left_foot_contact`/`right_foot_contact` from the database's existing per-frame contact data |

### Summary of fixes
1. Restored two static `MMFeatureExtractor` methods that were silently dropped during the prior rig-detection consolidation, unblocking compilation of `animation_library.hpp` and `editor/database_editor.cpp`. Both delegate to `MMClipAnalyzer` (the canonical classifier) rather than reintroducing any deleted system.
2. Connected the foot-contact data that already existed in the database all the way out to `get_debug_info()`, which `demo/player.gd` and `MMFootIKModifier` were already correctly written to consume — the fix was entirely at the missing middle link, nothing else in the chain needed to change.
3. Performed a full mechanical symbol/include/registration audit of the entire codebase (not just the two known bugs) and found no further issues to fix.

### Updated project completion percentage

Using the same per-subsystem methodology as `AUDIT_REPORT.md`:

- **§3 Feature Extraction:** 75% → **90%** (compile blocker removed; multi-threading gap from the original audit still open, unchanged, correctly out of scope for this pass)
- **§14 Foot IK:** 65% → **90%** (data path fixed end to end; algorithms were already complete)
- **§15 Character Controller Integration:** 70% → **80%** (foot contact now genuinely functional; traversal/motion-warp wiring gaps remain, correctly out of scope)
- **§17 ClipAnalyzer:** 85% → **90%** (the "transitively broken" caveat from the original audit no longer applies)
- **§18 Universal Humanoid Rig Support:** 85% → **90%** (clip-tagging convenience path no longer blocked)
- **§21 Database Builder:** 75% → **90%** (compile blocker removed; editor Database tab should now build)
- **§34 Build System:** 65% → 65% (unchanged — the scripts were never the problem, and this pass still could not obtain a real green build to raise this further; see §6)

**Overall completion (mean across the 35 scored subsystems, excluding §19 which is N/A by design): ≈ 77.6% → ≈ 80.1%**

This is a real but modest increase, and deliberately so — this pass fixed exactly two confirmed defects and verified there were no others of the same kind, without touching any of the Priority 2/3 items (dead cache/traversal/warp/profiler integrations, thin error handling, missing threading, test coverage gaps) that make up most of the remaining gap to a genuinely finished AAA baseline. Those remain fully logged and prioritized in `AUDIT_REPORT.md` for the next pass.
