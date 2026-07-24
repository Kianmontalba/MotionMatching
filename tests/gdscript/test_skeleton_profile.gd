extends RefCounted
## Verifies MMSkeletonProfile resolves hips and both feet on a skeleton whose
## bone names carry no semantic meaning at all ("bone_00".."bone_09") —
## this only passes if structural analysis (graph shape, not names) is doing
## the work, which is the whole point of universal rig support.

const SyntheticSkeleton = preload("res://../tests/gdscript/synthetic_skeleton.gd")

func run() -> bool:
	var skeleton := SyntheticSkeleton.build(SyntheticSkeleton.DEFAULT_NAMES)
	var profile := MMSkeletonProfile.new()

	var detected: bool = profile.auto_detect(skeleton)
	if not detected:
		push_error("test_skeleton_profile: auto_detect() returned false on a valid synthetic biped")
		return false

	var missing: PackedStringArray = profile.get_missing_roles()
	if not missing.is_empty():
		push_error("test_skeleton_profile: missing roles despite unambiguous structure: %s" % [missing])
		return false

	var pelvis: String = profile.get_bone_name(MMBoneRole.MM_BONE_PELVIS)
	if pelvis != SyntheticSkeleton.DEFAULT_NAMES["pelvis"]:
		push_error("test_skeleton_profile: pelvis resolved to '%s', expected '%s'" % [pelvis, SyntheticSkeleton.DEFAULT_NAMES["pelvis"]])
		return false

	var foot_l: String = profile.get_bone_name(MMBoneRole.MM_BONE_LEFT_FOOT)
	var foot_r: String = profile.get_bone_name(MMBoneRole.MM_BONE_RIGHT_FOOT)
	if foot_l == foot_r:
		push_error("test_skeleton_profile: left and right foot resolved to the same bone")
		return false
	if foot_l != SyntheticSkeleton.DEFAULT_NAMES["foot_l"] or foot_r != SyntheticSkeleton.DEFAULT_NAMES["foot_r"]:
		push_error("test_skeleton_profile: foot roles did not resolve to the expected bones (l=%s r=%s)" % [foot_l, foot_r])
		return false

	print("  ok: structural detection resolved pelvis + both feet with no semantic bone names")
	return true
