extends SceneTree
## Headless test runner for the MOTION MATCHING addon.
##
## Usage:
##   godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd
##
## Exits with code 0 if every test passes, 1 otherwise — safe to wire into
## CI as a build step after compiling the extension.

const TEST_SCRIPTS := [
	"res://../tests/gdscript/test_skeleton_profile.gd",
	"res://../tests/gdscript/test_skeleton_profile_aliases.gd",
	"res://../tests/gdscript/test_clip_analyzer.gd",
	"res://../tests/gdscript/test_database_roundtrip.gd",
	"res://../tests/gdscript/test_pose_search.gd",
	"res://../tests/gdscript/test_kdtree_vs_bruteforce.gd",
	"res://../tests/gdscript/test_root_motion_continuity.gd",
	"res://../tests/gdscript/test_resource_validation.gd",
]

func _initialize() -> void:
	print("MOTION MATCHING — running %d test scripts\n" % TEST_SCRIPTS.size())

	var passed := 0
	var failed := 0

	for path in TEST_SCRIPTS:
		var script: GDScript = load(path)
		if script == null:
			printerr("FAIL  %s  (could not load script)" % path)
			failed += 1
			continue

		var instance: RefCounted = script.new()
		if not instance.has_method("run"):
			printerr("FAIL  %s  (no run() method)" % path)
			failed += 1
			continue

		var ok: bool = instance.run()
		if ok:
			print("PASS  %s" % path.get_file())
			passed += 1
		else:
			printerr("FAIL  %s" % path.get_file())
			failed += 1

	print("\n%d passed, %d failed" % [passed, failed])
	quit(1 if failed > 0 else 0)
