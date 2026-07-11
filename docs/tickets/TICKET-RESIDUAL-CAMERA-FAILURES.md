# TICKET-RESIDUAL-CAMERA-FAILURES — post-TICKET-120 camera test residuals

## Stato
OPEN

## Priorità
P1

## Problema
Residual camera-related test failures remaining after the TICKET-120 PARTIAL closure. The user enumerated the residuals during a post-120 audit: `MotionBlurTorture` framebuffer mismatch, `GraphCache` counters incoherent, and 2 failures "to classify" (TBD). The test runner is env-blocked on this dev box (vcpkg_bootstrap path missing per `CHRONON3D_VCPKG_TOOLCHAIN_FILE` points to non-existent `vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`), so the 2 TBD failures cannot be captured in this commit. This ticket aggregates the evidence already on disk + provides a forward-point for the 2 TBD classification + re-enable of the 4 skipped `* doctest::skip()` tests in `test_graph_cache.cpp`.

## Evidenza

**Machine-verified (this commit, 2026-07-11)**:

- 4 `* doctest::skip()` tests in `tests/render_graph/pipeline/test_graph_cache.cpp` (the user-named "GraphCache counters incoerenti" residual):
  1. `"GraphCache - cache hit on structurally identical frames"` — line 43, `* doctest::skip()`
  2. `"GraphCache - cache miss when dimensions change"` — line 74, `* doctest::skip()`
  3. `"GraphCache - cache miss when layer added"` — line 103, `* doctest::skip()`
  4. `"GraphCache - pixel output matches non-cached path"` — line 137, `* doctest::skip()`
  - All 4 carry the inline comment: `// DISABLED: TICKET-120 — graph cache hit/miss counters are sensitive to internal engine implementation details and test ordering in unity builds. The counters produce non-deterministic values across test runs.`
  - The 4 tests verify `renderer.counters()->graph_cache_hits.load()` / `graph_cache_misses.load()` invariants against the CompiledGraphCache contract (counter non-determinism + pixel-output equivalence).
  - Inline ticket-metadata: `// TICKET-120 | Issue: graph-cache-counter-non-determinism | Owner: scene-camera-team | Motivation: counter order-sensitivity in unity builds (DISABLED above) | Data introduzione: 2026-05 | Deadline rimozione: TICKET-120`
- TICKET-120 itself is `PARTIAL` in `docs/FOLLOWUP_TICKETS.md` §Open Blockers, with the description `18/24 scene test failures remaining` — this ticket is the sub-cluster for the post-120 residuals.
- `git log --oneline -- tests/render_graph/pipeline/test_graph_cache.cpp` shows the 4 `* doctest::skip()` annotations are pre-existing on `main` (no recent commit modified them) — confirms these are inherited residuals, not regressions of a recent fix.

**User-enumerated (this commit, post-120 audit — user-spec verbatim)**:

- `MotionBlurTorture` framebuffer mismatch — the test name `MotionBlurTorture` is the user's own classification; the exact source file for this residual is NOT in the code-search results (machine-verified: `rg -nE 'MotionBlurTorture' --glob '*.cpp' --glob '*.hpp'` returned 0 matches). Identification requires a working build host.
- 2 TBD failures "da classificare" — user acknowledges they do not know what these are; they require test execution to identify (env-blocked).

**Env-blocker (this commit, machine-verified)**:

- `bash build-fast.sh test "*Camera*"` → CMake error: `CHRONON3D_VCPKG_TOOLCHAIN_FILE points to a non-existent path: /home/pierone/src/go-master/projects/Pyt/Chrono3d/cmake/../vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake`
- `bash build-fast.sh scene-test "*amera*"` → same error
- The blocker is `vcpkg_bootstrap` missing on this dev box (not bootstrapped). This blocks the capture of the 2 TBD failures + the actual test output for the 4 skipped tests + the source file for `MotionBlurTorture`.

## Impatto
Camera V1 cert (TICKET-120 PARTIAL closure is the parent; this ticket is the sub-cluster for the post-120 residuals). The 4 skipped tests verify the CompiledGraphCache contract (counter non-determinism + pixel-output equivalence); without them, the cache is not regression-locked. The `MotionBlurTorture` residual suggests a framebuffer-level mismatch that may regress AE-parity cinematic. The 2 TBD failures are unknown but counted in the camera failure tally (the TICKET-120 baseline says `18/24` failures — this ticket captures the subset that the user enumerated during the post-120 audit).

## Confine
- This ticket is a doc-only aggregator + forward-point tracker. **ZERO source-code modifications.**
- Re-enable of the 4 skipped tests in `test_graph_cache.cpp` is OUT-OF-SCOPE for this commit (deferred to working build host per AGENTS.md §honesty).
- Identification of the 2 TBD failures is OUT-OF-SCOPE for this commit (deferred to working build host).
- The `MotionBlurTorture` test fixture is OUT-OF-SCOPE for this commit (deferred to working build host).
- The TICKET-120 PARTIAL closure is NOT in scope; this ticket is a sub-cluster, not a superseder.

## Soluzione accettabile
Working build host run that:
1. Captures the 2 TBD failures + the actual output of the 4 skipped tests + the `MotionBlurTorture` source file (via `rg -n 'MotionBlurTorture' --glob '*.cpp' --glob '*.hpp' --glob '*.txt' --glob '*.md'` across the full repo + content dir).
2. Re-enables the 4 skipped tests in `test_graph_cache.cpp` (remove `* doctest::skip()` + the `// DISABLED: TICKET-120` comments) once the counter non-determinism is investigated + the engine's counter-ordering is locked.
3. Closes the ticket by replacing the 4+1+2=7 aggregated residuals with PASS-or-justified-skip verdicts.
4. Cross-link each verdict to a follow-up fix or a `// TICKET-XXX | ...` per-test metadata block (per the AGENTS.md v0.1 `// drift-allow: ticket-template-pattern` precedent).

## Criteri di accettazione
- 2 TBD failures identified by TEST_CASE name + file path + line number.
- 4 skipped tests in `test_graph_cache.cpp` re-enabled (or justified-skip documented with a per-test `// TICKET-XXX | ...` metadata block).
- `MotionBlurTorture` test source identified + root cause classified (framebuffer mismatch on which assertion? which fixture? which frame?).
- ≥4 of the 4+1+2=7 enumerated residuals are PASS or justified-skip (locks the scope to this ticket's enumeration, NOT the global TICKET-120 tally — the global tally may shift if a future camera fix lands before this ticket closes).
- `bash build-fast.sh test "*Camera*"` runs green on the residual subset (modulo any pre-existing rot tickets tracked separately).

## Collegamenti
- **Parent ticket**: [TICKET-120](../FOLLOWUP_TICKETS.md) (`18/24 scene test failures remaining`, PARTIAL — runtime blocks Camera V1 cert).
- **Closure candidate**: TICKET-120 PARTIAL → CLOSED once this ticket's 4+1+2 residuals are resolved.
- **Build host requirement**: working `vcpkg_bootstrap` (currently missing on this dev box; same blocker as Phase 1.C-redux / TICKET-007.h / OrientAlongPath-noop).
- **Index entry**: [docs/FOLLOWUP_TICKETS.md](../FOLLOWUP_TICKETS.md) §Open Blockers (one-line row added in this commit per user spec "una riga di indice one-line ... no duplicato del contenuto").
- **Cross-references**:
  - `docs/CHANGELOG.md` will get a closure entry once the residuals are resolved (per AGENTS.md v0.1 Cat-5 3-doc same-commit invariant).
  - `docs/CURRENT_STATUS.md` §Stato generale per area "Camera V1" row may get a follow-up note once the residuals are addressed (out-of-scope for this commit).
