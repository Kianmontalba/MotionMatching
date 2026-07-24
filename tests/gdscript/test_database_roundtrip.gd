extends RefCounted
## Builds a tiny database from two hand-authored clips (an idle and a
## forward-moving walk) on the synthetic skeleton, and checks the database's
## structural properties: frame count, animation count, and that the two
## clips end up with different tags because they measurably move
## differently.

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
	# Idle: root never moves.
	library.add_animation("idle", _make_clip([Vector3.ZERO, Vector3.ZERO, Vector3.ZERO], 1.0))
	# Walk: root advances 1.5 hip-heights over 1 second, comfortably inside
	# the default walk band (0.10 .. 1.50).
	library.add_animation("walk_forward", _make_clip(
		[Vector3.ZERO, Vector3(0, 0, -0.75), Vector3(0, 0, -1.5)], 1.0))

	var extractor := MMFeatureExtractor.new()
	var schema := MMFeatureSchema.new()
	if not schema.apply_skeleton_profile(skeleton):
		push_error("test_database_roundtrip: schema could not resolve pose bones from synthetic skeleton")
		return false
	extractor.set_schema(schema)

	var database: MotionMatchingDatabase = extractor.build_database(skeleton, library, {})
	if database == null:
		push_error("test_database_roundtrip: build_database returned null")
		return false

	if database.get_animation_count() != 2:
		push_error("test_database_roundtrip: expected 2 animation entries, got %d" % database.get_animation_count())
		return false

	if database.get_frame_count() <= 0:
		push_error("test_database_roundtrip: database has no frames")
		return false

	var idle_entry: MMAnimationEntry = database.get_animation_entry(0)
	var walk_entry: MMAnimationEntry = database.get_animation_entry(1)
	if idle_entry.tags == walk_entry.tags:
		push_error("test_database_roundtrip: idle and walk clips produced identical tags (%d) — motion measurement is not differentiating them" % idle_entry.tags)
		return false

	if not (walk_entry.tags & MMTag.MM_TAG_WALK):
		push_error("test_database_roundtrip: forward-moving clip was not tagged WALK (tags=%d)" % walk_entry.tags)
		return false

	print("  ok: database built with %d frames across 2 clips, tags differentiate idle from walk" % database.get_frame_count())
	return true
