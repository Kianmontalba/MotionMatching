# Public Beta Checklist — v0.1.0-beta

Status as of this audit. Checked items are genuinely met; unchecked items
are genuinely not, regardless of how close they might otherwise seem.

## Required before v0.1.0-beta release

- [ ] **Successful builds** — NOT MET. No build has ever completed in any
      environment that produced this project's documentation. This is the
      root blocker for nearly every other item below.
- [ ] **Runtime tested** — NOT MET. No test (manual or the 8 existing
      GDScript scripts) has ever been executed.
- [x] **Documentation updated** — MET for design/architecture/API
      reference (`docs/CLASS_REFERENCE_01–04.md`, `docs/SYSTEMS.md`,
      `docs/ARCHITECTURE_AND_WORKFLOW.md`, `docs/EDITOR_GUIDE.md`). NOT
      independently confirmed accurate against a running build, since
      none exists — documentation accuracy here means "consistent with
      the source code," not "confirmed against observed behavior."
- [x] **Known bugs documented** — MET. `docs/BETA_RELEASE_AUDIT.md` and
      its predecessor documents record every issue found by source
      inspection, including two real, fixed defects (a use-after-free
      race in `set_resource()`, a dead-code defect in
      `notify_frame_jump()`) and several unfixed items (dead signal, dead
      enum, naming inconsistencies, unbound constants).
- [x] **Installation guide completed** — MET at the level of "commands
      and prerequisites are documented" (`docs/HANDOFF_PACKAGE.md`). NOT
      MET at the level of "confirmed to actually produce a working build
      when followed," since no one has followed it end-to-end.
- [ ] **Example project included** — NOT MET. No runnable example
      project currently exists.
- [x] **License included** — MET. `LICENSE` (MIT) and `AUTHORS` present.
- [x] **Changelog updated** — MET. `CHANGELOG_BETA.md` produced this pass.

**Score: 4 of 8 met.** The two unmet items with the widest blast radius
(successful builds, runtime tested) are prerequisites for meaningfully
completing several of the "met" items too — e.g., an installation guide
can't really be called complete until someone has followed it and
confirmed it works.

## Recommended gate before proceeding to public beta

1. Obtain one successful build, on one real machine, and record the
   exact toolchain used.
2. Run the existing 8 GDScript tests against that build; record the
   pass/fail count honestly, including if some fail.
3. Manually exercise at minimum: idle/walk/run/sprint locomotion, one
   clip switch, one root-motion-across-a-switch check (this specifically
   validates the `notify_frame_jump()` fix), and — if async search is
   enabled — the `set_resource()`-while-searching stress case (this
   specifically validates the use-after-free fix).
4. Only after steps 1–3: revisit `docs/BETA_RELEASE_AUDIT.md`'s
   classification table and upgrade any item from NOT YET VERIFIED to
   VERIFIED BY RUNTIME TEST based on what was actually observed — not
   based on what was expected to happen.
5. Produce a runnable example project once a build exists to test it
   against.
6. Only then: consider this checklist's remaining unmet items again.

## What this checklist deliberately does not do

It does not mark any item "met" based on source-level confidence alone,
even where that confidence is high (e.g., the core search pipeline has
been reviewed multiple times across this project's history with no
unresolved defect found) — "met" here means the specific, literal
requirement in the request was satisfied, and "successful builds" /
"runtime tested" are binary facts about whether an execution happened,
not gradable by how likely success seems.
