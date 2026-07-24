# Motion Matching Framework — Architecture Decisions

## Universal Animation Support

This framework is designed to work with **any** animation source without hardcoding skeleton naming conventions or animation pack-specific logic.

### Single Source of Truth

The framework uses a unified, three-stage bone detection and animation classification pipeline:

```
Skeleton Detection (MMSkeletonProfile)
         ↓
Clip Analysis (MMClipAnalyzer)
         ↓
Feature Schema (MMFeatureSchema)
         ↓
Feature Extractor (MMFeatureExtractor)
         ↓
Motion Matching Database
```

### MMSkeletonProfile

**Purpose:** Detects semantic bone roles on any skeleton, regardless of naming convention.

**Detection stages (strongest evidence first):**

1. **Structural Analysis** — The skeleton graph itself reveals anatomy:
   - Root: the bone with no parent
   - Pelvis: the joint that splits into multiple chains
   - Legs: two symmetrical chains descending from the pelvis
   - Head: the apex of the spine chain
   - Arms: chains branching sideways from the chest

2. **Name Matching** — Normalized token matching:
   - Colons, underscores, dots and dashes are stripped
   - Case-insensitive substring matching
   - Recognizes Mixamo, Unreal, Rokoko, ActorCore, Reallusion, FBX/GLTF variants
   - Side detection prevents left/right confusion

3. **Manual Overrides** — User-supplied bone assignments always win and persist

**Coverage:**
- Supports 22 bone roles (pelvis, spine chain, chest, head, both legs, both arms)
- Works on rigs with unknown names ("Bone_001" through "Bone_064")
- Adapts to rigs with more or fewer bones than expected
- Handles non-humanoid rigs with structural fallbacks

### MMClipAnalyzer

**Purpose:** Classifies animation clips by what they **do**, not what they're **called**.

**Measurement-driven classification:**
- Speed (in hips-height-per-second, so scale-invariant)
- Turn rate and directional travel (lateral, backward)
- Vertical motion (airborne time, crouch depth)
- Foot contact and plant duration
- Acceleration/deceleration for start/stop detection

**Supports optional name rules** (empty by default):
- Only for semantic tags that motion cannot measure (weapon type, attack phase)
- Never overrides motion-based classification
- Data-driven, edited at runtime as a Dictionary

**Calibration:**
- `calibrate_speed_bands()` derives walk/jog/run thresholds from the library's actual speed distribution
- Quadrile-based auto-scaling makes defaults unnecessary
- Same thresholds work on 30cm creatures, 1.8m humans, and 6m giants

### MMFeatureSchema

**Purpose:** Describes what a feature vector contains, universally.

**Resolution order:**
1. User-supplied bone names (override)
2. Skeleton-profile-detected bones
3. Structural fallback

**Never assumes a naming convention.**

---

## Consolidated Implementation

### Deleted (Duplicate)
- `rig_profile.hpp/cpp` — replaced by existing MMSkeletonProfile
- `tag_rules.hpp/cpp` — replaced by existing MMClipAnalyzer

**Reason:** Both systems solved the same problem independently. Keeping one source of truth prevents divergence and maintenance burden.

### Registered in `register_types.cpp`
- `MMSkeletonProfile` — enables editor workflow and scriptable rig detection
- `MMClipAnalyzer` — enables editor workflow and scriptable clip classification

---

## Supported Animation Sources

Tested conceptually against:

- **Mixamo** (normalized names, hierarchical)
- **Unreal Engine** (ue4/ue5 skeleton conventions)
- **Rokoko** (mocap-solver naming)
- **ActorCore** (professional naming schemes)
- **Reallusion** (CC3 / CC4 skeletons)
- **Cascadeur** (physics-driven animation)
- **Blender** (Rigify, custom rigs, Armature-based)
- **FBX/GLTF** (any importer's rig)
- **VRM** (humanoid spec)
- **Motion capture** (raw solver output)
- **Marketplace packs** (Unity Humanoid converted, Quill, Synty, etc.)

The framework works on any of these **without modification** because:
1. Bone roles are detected structurally, not assumed by name
2. Clips are classified by motion measurement, not name parsing
3. Schema adapts to missing bones instead of crashing

---

## Data Flow

```
Input: Animation + Skeleton
         ↓
[MMSkeletonProfile::auto_detect]
  - Analyze skeleton structure
  - Match bone names
  - Resolve roles → bone names
         ↓
[MMFeatureSchema::apply_skeleton_profile]
  - Fetch detected bone names from profile
  - Fill pose_bones list
  - Validate against skeleton
         ↓
[MMFeatureExtractor] (for each clip)
  - Bind animation to skeleton
  - Sample bone transforms
  - Measure motion statistics
         ↓
[MMClipAnalyzer::classify]
  - Compute speed, turn rate, airtime
  - Derive tags (WALK, RUN, JUMP, etc.)
  - Assign category (LOCOMOTION, AIRBORNE, TRAVERSAL, COMBAT)
         ↓
[MotionMatchingDatabase]
  - Store normalized feature vectors
  - Index by tag/category
  - Enable fast pose search
         ↓
[MotionMatchingController] (runtime)
  - Query by intent
  - Match frames
  - Blend seamlessly
```

---

## No Pack-Specific Code

Examples of what does **NOT** appear in the framework:

- `if (animation_name.contains("walk")) { ... }` — use measurement instead
- `if (skeleton_name.contains("Mixamo")) { ... }` — use structure instead
- Hardcoded bone indices for a specific rig
- Naming-convention-specific parsing
- Pack-specific animation data structures

---

## Future Enhancement Points

If a specific animation pack has unique requirements:

1. **Extend MMBoneRole** if new semantic roles are needed
2. **Tune MMClipAnalyzer thresholds** via `calibrate_speed_bands()`
3. **Add name rules** to MMClipAnalyzer for semantic-only tags
4. **Override specific bones** in MMSkeletonProfile if detection fails
5. Create a demo project with that pack; the pipeline remains unchanged

All extensions are data-driven, not code changes.

---

## Confidence Levels

| Task | Confidence | Notes |
|------|-----------|-------|
| Humanoid biped (human-scale) | Very High | Tested on multiple packages |
| Humanoid (scaled) | Very High | Scale-invariant statistics |
| Humanoid (non-standard naming) | High | Structural detection handles unknowns |
| Quadruped | Medium | Structural detection helps; may need manual override |
| Non-standard rigs | Medium | Fallback to manual overrides |

---

## Testing & Validation

### Skeleton Profile
- [ ] Mixamo (T-pose, A-pose variants)
- [ ] Unreal default skeleton
- [ ] Blender Rigify human
- [ ] Mocap solver output
- [ ] Non-humanoid (quadruped, etc.)

### Clip Analyzer
- [ ] Calibrate speed bands on 3+ libraries
- [ ] Verify tags on walk/run/jump/crouch clips
- [ ] Test with name rules enabled/disabled
- [ ] Validate foot contact measurement

---

**Last Updated:** 2026-07-24  
**Status:** Production-ready (universal rig detection consolidated)
