extends RefCounted
## Verifies that MMRootMotion.notify_frame_jump() makes the very next
## update() tick adopt the new frame's root velocity immediately, rather
## than easing into it over blend_halflife like an ordinary continuous
## tick would. This is root motion's specific continuity guarantee across a
## motion-matching switch: pose and velocity should never disagree about how
## fast the character is currently moving.
##
## This test would have failed before the fix in root_motion.cpp: previously
## notify_frame_jump() wrote to _blend_linear/_blend_angular, but update()
## never read those fields, so a jump was silently still eased in over
## blend_halflife like any other tick.

const SyntheticSkeleton = preload("res://../tests/gdscript/synthetic_skeleton.gd")

func _make_two_speed_clip() -> Animation:
	var animation := Animation.new()
	animation.length = 2.0
	animation.loop_mode = Animation.LOOP_NONE
	var track := animation.add_track(Animation.TYPE_POSITION_3D)
	animation.track_set_path(track, NodePath("%GeneralSkeleton:" + SyntheticSkeleton.DEFAULT_NAMES["root"]))
	# First second: 1.0 units/sec. Second second: 4.0 units/sec. Clearly
	# distinct speeds so a "still blending from the old one" failure is easy
	# to detect rather than lost in noise.
	animation.track_insert_key(track, 0.0, Vector3.ZERO)
	animation.track_insert_key(track, 1.0, Vector3(0, 0, -1.0))
	animation.track_insert_key(track, 2.0, Vector3(0, 0, -5.0))
	return animation

func run() -> bool:
	var skeleton := SyntheticSkeleton.build()
	skeleton.name = "GeneralSkeleton"
	skeleton.unique_name_in_owner = true

	var library := AnimationLibrary.new()
	library.add_animation("two_speed", _make_two_speed_clip())

	var schema := MMFeatureSchema.new()
	if not schema.apply_skeleton_profile(skeleton):
		push_error("test_root_motion_continuity: schema could not resolve pose bones")
		return false

	var extractor := MMFeatureExtractor.new()
	extractor.set_schema(schema)
	var database: MotionMatchingDatabase = extractor.build_database(skeleton, library, {})
	if database == null or database.get_frame_count() == 0:
		push_error("test_root_motion_continuity: database build failed")
		return false
	database.finalize(schema.get_dimension())

	var slow_frame: int = database.get_frame_at_time(0, 0.2)
	var fast_frame: int = database.get_frame_at_time(0, 1.8)
	if slow_frame < 0 or fast_frame < 0:
		push_error("test_root_motion_continuity: could not resolve reference frames")
		return false

	var slow_speed: float = database.get_frame_root_velocity_value(slow_frame).length()
	var fast_speed: float = database.get_frame_root_velocity_value(fast_frame).length()
	if fast_speed < slow_speed * 2.0:
		push_error("test_root_motion_continuity: fast/slow frames are not distinct enough (%f vs %f) — synthetic clip did not produce the expected speed contrast" % [fast_speed, slow_speed])
		return false

	var root_motion := MMRootMotion.new()
	root_motion.set_database(database)
	root_motion.blend_halflife = 0.08

	# Let velocity converge to the slow frame's speed over several ticks of
	# ordinary (non-jump) playback.
	for i in 20:
		root_motion.update(1.0 / 60.0, slow_frame, -1, 1.0)
	root_motion.consume_delta() # discard, we only care about steady-state velocity

	# Now jump straight to the fast frame and take exactly one tick.
	root_motion.notify_frame_jump(fast_frame)
	var delta: float = 1.0 / 60.0
	root_motion.update(delta, fast_frame, -1, 1.0)
	var jump_translation: Vector3 = root_motion.consume_delta().origin

	var observed_speed: float = jump_translation.length() / delta
	# Should already be close to the fast frame's speed, not still near the
	# slow frame's speed (which is what the bug produced: a single
	# blend_halflife-smoothing step barely moves off the old velocity).
	var midpoint: float = (slow_speed + fast_speed) * 0.5
	if observed_speed < midpoint:
		push_error("test_root_motion_continuity: velocity after notify_frame_jump() was %f, still closer to the pre-jump speed (%f) than the new one (%f) — jump was not adopted immediately" % [observed_speed, slow_speed, fast_speed])
		return false

	print("  ok: root motion adopted the new frame's velocity (%.2f) immediately after notify_frame_jump(), instead of easing in from %.2f" % [observed_speed, slow_speed])
	return true
