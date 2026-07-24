# MOTION MATCHING v0.1.0-alpha — Test Checklist

For the developer to work through after compiling this package on a real
machine. Nothing here has been pre-checked — every box is unchecked
because nothing has been run yet. Check items off as you personally
confirm them; note the outcome (pass/fail/partial) even for items that
don't fully pass, since a real "it half-works this way" note is more
useful for the next fix than a silently skipped checkbox.

## Compilation
- [ ] `scons platform=<yours> target=template_debug` completes with no
      errors
- [ ] `scons platform=<yours> target=template_release` completes with no
      errors
- [ ] Output binary appears in `demo/addons/motion_matching/bin/`
- [ ] (Optional) CMake build (`cmake -B build -DMM_BUILD_EDITOR=ON &&
      cmake --build build`) also completes, if you use it for IDE indexing

## Editor Loading
- [ ] Godot editor opens the project with no GDExtension load errors in
      the console
- [ ] All 28 classes appear in Create Node / New Resource dialogs
- [ ] The **Motion Matching** bottom panel appears
- [ ] Each of the 4 sub-tools in that panel (Database, Feature,
      Trajectory, Debug) is reachable

## Database Generation
- [ ] Point the database editor at a `Skeleton3D` and `AnimationLibrary`
- [ ] "Scan" completes and produces a reviewable tag/category table
- [ ] "Build database" completes without error
- [ ] "Save" produces a `.tres` file on disk

## Rig Detection
- [ ] Test against at least one real rig you have available (any
      convention — Mixamo, UE, a custom rig, whatever you have on hand;
      this repository currently contains no sample rig files)
- [ ] `get_detection_report()` / `get_missing_roles()` output makes sense
      for that rig
- [ ] Left/right bones are correctly disambiguated

## Animation Import
- [ ] Standard Godot import produces usable `Animation` resources (this
      is Godot's own importer, not addon code — confirm no interference)

## Feature Extraction
- [ ] `build_database()` completes on your test library
- [ ] Resulting database's frame count is non-zero and matches
      expectations for your library's total length × sample rate

## KD-Tree Build
- [ ] `MMPoseSearch.build()` / `is_built()` returns true after `rebuild()`

## Search
- [ ] A query built from real character movement returns a plausible
      match (not a wildly wrong clip)
- [ ] `search_query()` and `search_brute_force_query()` agree on the same
      query (this is exactly what `test_kdtree_vs_bruteforce.gd` checks —
      run that test specifically)

## Runtime Playback
- [ ] Idle / Walk / Run / Sprint / Strafe / Backward / Turning /
      Stopping / Acceleration / Deceleration all produce a plausible
      clip and no visible animation restart-from-zero

## Root Motion
- [ ] No positional pop across a clip switch (this specifically tests
      the `notify_frame_jump()` fix made this project cycle)
- [ ] No sliding during steady locomotion

## Foot IK
- [ ] Correct planting on flat ground
- [ ] Adapts to a slope/stairs if you have test geometry
- [ ] Locking/release timing looks correct against a planted-foot clip

## Aim IK
- [ ] Chain rotates smoothly toward a moving target
- [ ] Respects the cone limit (does not over-rotate)

## Motion Warp
- [ ] `begin_warp()`/`end_warp()` produce a visible curve toward the
      target, not a snap
- [ ] Specifically test a clip switch occurring mid-warp and record what
      actually happens — this is a known, explicitly unresolved edge
      case; there is no "expected correct" answer yet, only "what did you
      observe"

## Traversal
- [ ] `traversal_requested` signal fires when approaching real obstacle
      geometry
- [ ] Fires with a plausible type and target point
- [ ] Does not false-positive on flat, unobstructed ground

## Search Cache
- [ ] `get_debug_info()["cache_hits"]` increases when the character holds
      still
- [ ] A category lock or traversal detection produces a cache miss even
      on an otherwise-repeated query

## Async Search
- [ ] Enabling `async_search` produces no crash
- [ ] `get_debug_info()` values update with roughly one tick of latency
- [ ] **Specifically stress-test:** call `set_resource()` repeatedly while
      async search is active and the character is moving — this
      exercises the exact use-after-free race found and fixed this
      project cycle

## Profiler
- [ ] `get_debug_info()["profiler"]` contains non-zero percentiles after
      a few seconds of movement
- [ ] Switch count increases as clips actually switch

## Memory Usage
- [ ] No unbounded RSS growth over several minutes of continuous play
- [ ] No crash after repeated `set_resource()` / scene reload /
      character spawn-despawn cycles

## Performance
- [ ] Record actual search time (`get_debug_info()["search_time_usec"]`)
- [ ] Record actual frame time / FPS with the addon active
- [ ] These are the first real numbers this project will ever have —
      write them down even if they're bad; a real bad number is more
      useful than no number

## Crash Testing
- [ ] Missing `AnimationTree` — confirm graceful behavior, not a crash
- [ ] Missing `Skeleton3D` — confirm graceful behavior
- [ ] Empty/null `MotionMatchingResource` — confirm `validate()` reports
      it and nothing crashes
- [ ] Deliberately broken rig (missing bones) — confirm
      `validate_library()` flags it
- [ ] Zero-length animation clip in the library — confirm it's flagged,
      not silently included
