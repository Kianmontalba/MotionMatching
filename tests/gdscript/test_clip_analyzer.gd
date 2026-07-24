extends RefCounted
## Verifies MMClipAnalyzer tags clips by measured motion, not by name — a
## clip literally named "TAKE_001" that moves fast is still tagged SPRINT,
## and a clip named "sprint_final" that barely moves is tagged IDLE, as long
## as use_name_rules is left off (the default).

func _stats(overrides: Dictionary) -> Dictionary:
	# Defaults describe a stationary clip; each test overrides only the
	# fields relevant to the motion being simulated. Hip height stays 1.0
	# throughout since MMClipAnalyzer's thresholds are already expressed in
	# hip-heights-per-second, making this scale-neutral by construction.
	var stats := {
		"duration": 1.0,
		"hip_height": 1.0,
		"average_speed": 0.0,
		"peak_speed": 0.0,
		"start_speed": 0.0,
		"end_speed": 0.0,
		"net_displacement": 0.0,
		"vertical_range": 0.0,
		"net_height_gain": 0.0,
		"total_turn": 0.0,
		"lateral_ratio": 0.0,
		"backward_ratio": 0.0,
		"contact_ratio": 1.0,
		"longest_airborne": 0.0,
		"crouch_ratio": 0.0,
	}
	for key in overrides:
		stats[key] = overrides[key]
	return stats

func run() -> bool:
	var analyzer := MMClipAnalyzer.new()
	analyzer.use_name_rules = false
	analyzer.use_motion_analysis = true

	# A clip called "TAKE_001" moving at 4.5 hip-heights/sec should tag as
	# SPRINT (default run_speed threshold is 3.80).
	var fast_stats := _stats({"average_speed": 4.5, "peak_speed": 4.8})
	var fast_tags: int = analyzer.classify_dictionary(fast_stats, "TAKE_001")
	if not (fast_tags & MMTag.MM_TAG_SPRINT):
		push_error("test_clip_analyzer: fast motion named 'TAKE_001' was not tagged SPRINT (tags=%d)" % fast_tags)
		return false

	# A clip called "sprint_final" that barely moves should tag as IDLE, not
	# SPRINT, because name rules are off.
	var slow_stats := _stats({"average_speed": 0.03, "peak_speed": 0.05})
	var slow_tags: int = analyzer.classify_dictionary(slow_stats, "sprint_final")
	if slow_tags & MMTag.MM_TAG_SPRINT:
		push_error("test_clip_analyzer: near-stationary motion named 'sprint_final' was incorrectly tagged SPRINT")
		return false
	if not (slow_tags & MMTag.MM_TAG_IDLE):
		push_error("test_clip_analyzer: near-stationary motion was not tagged IDLE (tags=%d)" % slow_tags)
		return false

	# An airborne clip with positive height gain should tag JUMP.
	var jump_stats := _stats({
		"longest_airborne": 0.4,
		"net_height_gain": 0.3,
		"contact_ratio": 0.1,
		"average_speed": 1.0,
	})
	var jump_tags: int = analyzer.classify_dictionary(jump_stats, "anim_47")
	if not (jump_tags & MMTag.MM_TAG_JUMP):
		push_error("test_clip_analyzer: airborne rising motion was not tagged JUMP (tags=%d)" % jump_tags)
		return false

	print("  ok: clip tags derived from motion, independent of clip name")
	return true
