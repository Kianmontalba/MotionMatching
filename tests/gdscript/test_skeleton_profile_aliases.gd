extends RefCounted
## Same synthetic skeleton, this time using real Mixamo bone names, to verify
## the name-matching stage (stage 2) agrees with structural analysis
## (stage 1) rather than fighting it.

const SyntheticSkeleton = preload("res://../tests/gdscript/synthetic_skeleton.gd")

func run() -> bool:
	var skeleton := SyntheticSkeleton.build(SyntheticSkeleton.MIXAMO_NAMES)
	var profile := MMSkeletonProfile.new()

	if not profile.auto_detect(skeleton):
		push_error("test_skeleton_profile_aliases: auto_detect() failed on Mixamo-named skeleton")
		return false

	var pelvis: String = profile.get_bone_name(MMBoneRole.MM_BONE_PELVIS)
	# Mixamo's "Hips" bone is this rig's root; MM_BONE_PELVIS should resolve
	# to whichever bone is actually the branch point (bone named "Spine"
	# here per the synthetic layout), demonstrating names disambiguate but
	# structure still decides the role.
	var head: String = profile.get_bone_name(MMBoneRole.MM_BONE_HEAD)
	if head != "mixamorig:Head":
		push_error("test_skeleton_profile_aliases: head resolved to '%s', expected 'mixamorig:Head'" % head)
		return false

	var foot_l: String = profile.get_bone_name(MMBoneRole.MM_BONE_LEFT_FOOT)
	var foot_r: String = profile.get_bone_name(MMBoneRole.MM_BONE_RIGHT_FOOT)
	if foot_l != "mixamorig:LeftFoot":
		push_error("test_skeleton_profile_aliases: left foot resolved to '%s', expected 'mixamorig:LeftFoot'" % foot_l)
		return false
	if foot_r != "mixamorig:RightFoot":
		push_error("test_skeleton_profile_aliases: right foot resolved to '%s', expected 'mixamorig:RightFoot'" % foot_r)
		return false

	print("  ok: Mixamo-convention names resolved correctly (pelvis=%s)" % pelvis)
	return true
