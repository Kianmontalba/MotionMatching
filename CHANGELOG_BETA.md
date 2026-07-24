# Changelog — v0.1.0-beta (preparation)

**Note on this changelog's honesty constraint:** this file only lists
changes that actually happened between the alpha audit and this beta
preparation pass. No build occurred in that window, so there is no
runtime-verification progress to report — that section says so plainly
rather than being silently omitted.

## [0.1.0-beta] — in preparation, not yet released

### Runtime verification progress since alpha
**None.** No build was attempted, no test was run, and no bug reports
were collected from real users between the alpha audit and this pass.
The project remains in exactly the same runtime-unverified state
described in `docs/ALPHA_RELEASE_AUDIT.md`.

### Findings confirmed still present (re-checked fresh for this pass)
- `search_completed` signal remains declared but never emitted.
- No new compiled objects or build artifacts exist anywhere in the
  repository.
- `demo/addons/motion_matching/bin/` still contains only `.gitkeep`.

### Documentation added since alpha
- `docs/BETA_RELEASE_AUDIT.md` — this beta preparation audit.
- `README_BETA.md`, `PUBLIC_BETA_CHECKLIST.md` (this same pass).

### Recommendations carried forward from the alpha audit, still open
- Fix or remove the dead `search_completed` signal.
- Fix or remove the unwired `MMPlaybackMode` enum.
- Rename 3 boolean accessors to match the codebase's own `is_X`/`has_X`
  convention before any public-facing release.
- Bind the missing `MM_TAG_USER_0`/`_1`/`_2`/`MM_TAG_NONE` constants.
- Confirm or refute the `-ffast-math` release-build risk with real data.
- Produce a runnable example project (none currently exists).

### What has not changed and should not be claimed as changed
No new performance numbers, no new compatibility confirmations, no newly
"stable" classification for any system. Every classification in
`docs/BETA_RELEASE_AUDIT.md` remains gated on the same missing runtime
evidence identified in the alpha audit.
