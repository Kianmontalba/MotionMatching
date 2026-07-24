extends RefCounted
## Builds a database with two clearly distinct clips (stationary vs. fast
## forward motion) and confirms a query built from a fast-forward trajectory
## matches a frame belonging to the moving clip, not the stationary one.
## This is the actual behavior the whole framework exists to guarantee, so
## it's worth asserting directly rather than trusting the unit pieces alone.

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

func run() -> bool:
	var skeleton := SyntheticSkeleton.build()
	skeleton.name = "GeneralSkeleton"
	skeleton.unique_name_in_owner = true

	var library := AnimationLibrary.new()
	library.add_animation("idle", _make_clip([Vector3.ZERO, Vector3.ZERO, Vector3.ZERO], 1.0))
	# Sprint: root advances 5 hip-heights over 1 second (well past run_speed 3.80).
	library.add_animation("sprint_forward", _make_clip(
		[Vector3.ZERO, Vector3(0, 0, -2.5), Vector3(0, 0, -5.0)], 1.0))

	var schema := MMFeatureSchema.new()
	if not schema.apply_skeleton_profile(skeleton):
		push_error("test_pose_search: schema could not resolve pose bones")
		return false

	var extractor := MMFeatureExtractor.new()
	extractor.set_schema(schema)
	var database: MotionMatchingDatabase = extractor.build_database(skeleton, library, {})
	if database == null or database.get_frame_count() == 0:
		push_error("test_pose_search: database build failed")
		return false
	database.finalize(schema.get_dimension())

	var search := MMPoseSearch.new()
	if not search.build(database):
		push_error("test_pose_search: MMPoseSearch.build() failed")
		return false

	var cost := MMCostFunction.new()
	cost.set_schema(schema)

	# Build a query whose trajectory-position group matches fast forward
	# motion, zeroed everywhere else, then let the database normalize it the
	# same way the stored frames were normalized.
	var raw_query := schema.make_vector()
	var traj_offset: int = schema.get_trajectory_position_offset()
	var point_count: int = schema.get_trajectory_point_count()
	for p in point_count:
		# -Z forward at roughly sprint speed, scaled by each sample's lookahead.
		raw_query[traj_offset + p * 2 + 1] = -4.5 * (0.2 + p * 0.2)

	var query := database.normalize_query(raw_query)
	var result: Dictionary = search.search_query(query, cost, 0, 0, -1)

	if not result.has("animation_id"):
		push_error("test_pose_search: search result missing 'animation_id' key")
		return false

	var matched_id: int = result["animation_id"]
	var matched_entry: MMAnimationEntry = database.get_animation_entry(matched_id)
	if not matched_entry.get_qualified_name().contains("sprint"):
		push_error("test_pose_search: fast-forward query matched '%s', expected the sprint clip" % matched_entry.get_qualified_name())
		return false

	print("  ok: fast-forward query matched the sprint clip, not idle")
	return true
