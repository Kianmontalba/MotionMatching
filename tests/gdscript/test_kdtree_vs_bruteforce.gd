extends RefCounted
## Confirms MMPoseSearch's KD-tree acceleration structure agrees with its own
## brute-force reference implementation (search_brute_force_query(), a new
## scriptable wrapper added alongside this test) on several distinct queries.
## This is the correctness guarantee the whole acceleration structure exists
## to preserve: it must never disagree with an exhaustive scan, only be
## faster than one.

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
	library.add_animation("walk_forward", _make_clip(
		[Vector3.ZERO, Vector3(0, 0, -0.75), Vector3(0, 0, -1.5)], 1.0))
	library.add_animation("jog_forward", _make_clip(
		[Vector3.ZERO, Vector3(0, 0, -1.4), Vector3(0, 0, -2.8)], 1.0))
	library.add_animation("sprint_forward", _make_clip(
		[Vector3.ZERO, Vector3(0, 0, -2.5), Vector3(0, 0, -5.0)], 1.0))
	library.add_animation("strafe_right", _make_clip(
		[Vector3.ZERO, Vector3(-0.9, 0, 0), Vector3(-1.8, 0, 0)], 1.0))

	var schema := MMFeatureSchema.new()
	if not schema.apply_skeleton_profile(skeleton):
		push_error("test_kdtree_vs_bruteforce: schema could not resolve pose bones")
		return false

	var extractor := MMFeatureExtractor.new()
	extractor.set_schema(schema)
	var database: MotionMatchingDatabase = extractor.build_database(skeleton, library, {})
	if database == null or database.get_frame_count() == 0:
		push_error("test_kdtree_vs_bruteforce: database build failed")
		return false
	database.finalize(schema.get_dimension())

	var search := MMPoseSearch.new()
	if not search.build(database):
		push_error("test_kdtree_vs_bruteforce: MMPoseSearch.build() failed")
		return false

	var cost := MMCostFunction.new()
	cost.set_schema(schema)

	var traj_offset: int = schema.get_trajectory_position_offset()
	var point_count: int = schema.get_trajectory_point_count()

	# A handful of distinct directions/speeds so the comparison isn't just
	# exercising one lucky branch of the tree.
	var directions := [
		Vector3(0, 0, -1) * 0.5,  # slow forward, near idle/walk boundary
		Vector3(0, 0, -1) * 3.0,  # fast forward, near jog/sprint boundary
		Vector3(0, 0, -1) * 5.5,  # past sprint
		Vector3(-1, 0, 0) * 2.0,  # sideways
		Vector3(0.3, 0, -0.8) * 1.5, # diagonal
	]

	for dir in directions:
		var raw_query := schema.make_vector()
		for p in point_count:
			var sample: Vector3 = dir * (0.2 + p * 0.2)
			raw_query[traj_offset + p * 2 + 0] = sample.x
			raw_query[traj_offset + p * 2 + 1] = sample.z

		var query := database.normalize_query(raw_query)

		var tree_result: Dictionary = search.search_query(query, cost, 0, 0, -1)
		var brute_result: Dictionary = search.search_brute_force_query(query, cost, 0, 0, -1)

		if tree_result.get("frame_index", -1) != brute_result.get("frame_index", -2):
			push_error("test_kdtree_vs_bruteforce: frame_index disagreement for direction %s — tree=%d brute=%d" % [
				dir, tree_result.get("frame_index", -1), brute_result.get("frame_index", -2)])
			return false

		var tree_cost: float = tree_result.get("cost", -1.0)
		var brute_cost: float = brute_result.get("cost", -2.0)
		if absf(tree_cost - brute_cost) > 0.0001:
			push_error("test_kdtree_vs_bruteforce: cost disagreement for direction %s — tree=%f brute=%f" % [
				dir, tree_cost, brute_cost])
			return false

	print("  ok: KD-tree search agreed with brute-force reference on %d distinct queries" % directions.size())
	return true
