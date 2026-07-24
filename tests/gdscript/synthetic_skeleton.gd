extends RefCounted
## Builds a minimal synthetic biped skeleton entirely in code, so the test
## suite never depends on an imported asset being present.
##
## Layout (12 bones), rest pose roughly standing upright with legs apart:
##
##   root
##    └─ pelvis
##        ├─ spine
##        │   └─ head
##        ├─ leg_upper_l ─ leg_lower_l ─ foot_l
##        └─ leg_upper_r ─ leg_lower_r ─ foot_r
##
## `names` lets callers pick the naming convention under test (unfamiliar
## tokens to force structural detection, or a known convention like Mixamo's
## to test the name-matching stage).

const DEFAULT_NAMES := {
	"root": "root",
	"pelvis": "bone_01",
	"spine": "bone_02",
	"head": "bone_03",
	"leg_upper_l": "bone_04",
	"leg_lower_l": "bone_05",
	"foot_l": "bone_06",
	"leg_upper_r": "bone_07",
	"leg_lower_r": "bone_08",
	"foot_r": "bone_09",
}

const MIXAMO_NAMES := {
	"root": "mixamorig:Hips",
	"pelvis": "mixamorig:Spine",
	"spine": "mixamorig:Spine1",
	"head": "mixamorig:Head",
	"leg_upper_l": "mixamorig:LeftUpLeg",
	"leg_lower_l": "mixamorig:LeftLeg",
	"foot_l": "mixamorig:LeftFoot",
	"leg_upper_r": "mixamorig:RightUpLeg",
	"leg_lower_r": "mixamorig:RightLeg",
	"foot_r": "mixamorig:RightFoot",
}

static func build(names: Dictionary = DEFAULT_NAMES) -> Skeleton3D:
	var skeleton := Skeleton3D.new()

	var root := skeleton.add_bone(names["root"])
	skeleton.set_bone_rest(root, Transform3D(Basis(), Vector3(0, 1.0, 0)))

	var pelvis := skeleton.add_bone(names["pelvis"])
	skeleton.set_bone_parent(pelvis, root)
	skeleton.set_bone_rest(pelvis, Transform3D(Basis(), Vector3(0, 0.0, 0)))

	var spine := skeleton.add_bone(names["spine"])
	skeleton.set_bone_parent(spine, pelvis)
	skeleton.set_bone_rest(spine, Transform3D(Basis(), Vector3(0, 0.3, 0)))

	var head := skeleton.add_bone(names["head"])
	skeleton.set_bone_parent(head, spine)
	skeleton.set_bone_rest(head, Transform3D(Basis(), Vector3(0, 0.4, 0)))

	# Left leg, offset on +X.
	var upper_l := skeleton.add_bone(names["leg_upper_l"])
	skeleton.set_bone_parent(upper_l, pelvis)
	skeleton.set_bone_rest(upper_l, Transform3D(Basis(), Vector3(0.15, -0.05, 0)))

	var lower_l := skeleton.add_bone(names["leg_lower_l"])
	skeleton.set_bone_parent(lower_l, upper_l)
	skeleton.set_bone_rest(lower_l, Transform3D(Basis(), Vector3(0, -0.45, 0)))

	var foot_l := skeleton.add_bone(names["foot_l"])
	skeleton.set_bone_parent(foot_l, lower_l)
	skeleton.set_bone_rest(foot_l, Transform3D(Basis(), Vector3(0, -0.45, 0.05)))

	# Right leg, offset on -X (mirrors left).
	var upper_r := skeleton.add_bone(names["leg_upper_r"])
	skeleton.set_bone_parent(upper_r, pelvis)
	skeleton.set_bone_rest(upper_r, Transform3D(Basis(), Vector3(-0.15, -0.05, 0)))

	var lower_r := skeleton.add_bone(names["leg_lower_r"])
	skeleton.set_bone_parent(lower_r, upper_r)
	skeleton.set_bone_rest(lower_r, Transform3D(Basis(), Vector3(0, -0.45, 0)))

	var foot_r := skeleton.add_bone(names["foot_r"])
	skeleton.set_bone_parent(foot_r, lower_r)
	skeleton.set_bone_rest(foot_r, Transform3D(Basis(), Vector3(0, -0.45, 0.05)))

	skeleton.reset_bone_poses()
	return skeleton
