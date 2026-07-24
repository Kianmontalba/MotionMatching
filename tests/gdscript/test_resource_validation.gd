extends RefCounted
## Verifies MotionMatchingResource.validate() reports the issues it's
## supposed to (empty resource: missing-database error) and reports no
## errors once schema+database+library are all consistently assigned.

const SyntheticSkeleton = preload("res://../tests/gdscript/synthetic_skeleton.gd")

func _make_clip(root_track_positions: Array, length: float) -> Animation:
	var animation := Animation.new()
	animation.length = length
	animation.loop_mode = Animation.LOOP_NONE
	var track := animation.add_track(Animation.TYPE_POSITION_3D)
	animation.track_set_path(track, NodePath("%GeneralSkeleton:" + SyntheticSkeleton.DEFAULT_NAMES["root"]))
	var step: float = length / float(root_track_positions.size() - 1)
	for i in root_track_positions.size():
		animation.track_insert_key(track, i * step, root_track_positions[i])
	return animation

func _has_severity(issues: Array, severity: String) -> bool:
	for issue in issues:
		if issue.get("severity", "") == severity:
			return true
	return false

func run() -> bool:
	# --- Empty resource: must report the missing-database error and stop
	# there (nothing else can be meaningfully checked without one). ---
	var empty_resource := MotionMatchingResource.new()
	var empty_issues: Array = empty_resource.validate()
	if not _has_severity(empty_issues, "error"):
		push_error("test_resource_validation: an empty resource reported no errors at all (issues=%s)" % [empty_issues])
		return false

	# --- Well-formed resource: schema, database, and library all agree. ---
	var skeleton := SyntheticSkeleton.build()
	skeleton.name = "GeneralSkeleton"
	skeleton.unique_name_in_owner = true

	var library := AnimationLibrary.new()
	library.add_animation("idle", _make_clip([Vector3.ZERO, Vector3.ZERO, Vector3.ZERO], 1.0))

	var schema := MMFeatureSchema.new()
	if not schema.apply_skeleton_profile(skeleton):
		push_error("test_resource_validation: schema could not resolve pose bones")
		return false

	var extractor := MMFeatureExtractor.new()
	extractor.set_schema(schema)
	var database: MotionMatchingDatabase = extractor.build_database(skeleton, library, {})
	if database == null or database.get_frame_count() == 0:
		push_error("test_resource_validation: database build failed")
		return false
	database.finalize(schema.get_dimension())

	var good_resource := MotionMatchingResource.new()
	good_resource.animation_library = library
	good_resource.schema = schema
	good_resource.database = database

	var good_issues: Array = good_resource.validate()
	if _has_severity(good_issues, "error"):
		push_error("test_resource_validation: a well-formed resource reported error-severity issues: %s" % [good_issues])
		return false

	# --- Deliberately mismatched schema: a second schema built from the same
	# skeleton should still differ in dimension from a swapped-in empty one,
	# giving us a reliable error to check for. ---
	var mismatched_resource := MotionMatchingResource.new()
	mismatched_resource.database = database
	mismatched_resource.schema = MMFeatureSchema.new() # dimension 0, not built from any skeleton
	var mismatched_issues: Array = mismatched_resource.validate()
	if not _has_severity(mismatched_issues, "error"):
		push_error("test_resource_validation: a schema/database dimension mismatch was not reported as an error (issues=%s)" % [mismatched_issues])
		return false

	print("  ok: validate() reported errors for empty/mismatched resources and none for a well-formed one")
	return true
