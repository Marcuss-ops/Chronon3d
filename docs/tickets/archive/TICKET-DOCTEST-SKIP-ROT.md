# TICKET-DOCTEST-SKIP-ROT — Skip registry (single authority for doctest::skip)

Status: RESOLVED (registry authority landed; all 10 original rows retired, 2026-09-24)
Owner: testing-infra
Created: 2026-09-07

## Purpose

Every `doctest::skip(...)` in `tests/` must cite this ticket plus the row id
below. The archive folder was removed on 2026-09-05 ("git history is the only
archive"), which orphaned the ticket references inside skip reasons
(TICKET-013, TICKET-012, TICKET-120, TICKET-007.i, TICKET-CABI-V2-VIDEO-CERT).
This file is the single live authority for skip rot: one row per intentionally
skipped test, with owner, deadline and reactivation condition. The registry is
currently empty. A skip without a row here is a lint failure; a row past its
deadline blocks the certification gate (CI FAIL, not a warning).

## Registry

| Row | Test (file :: TEST_CASE prefix) | Reason | Owner | Deadline | Reactivation condition |
|---|---|---|---|---|---|

## Rules

1. New `doctest::skip(...)` sites MUST add a row here and cite
   `TICKET-DOCTEST-SKIP-ROT SKIP-NNN` in the skip reason.
2. Rows past their deadline fail the certification aggregator (P0 blocker,
   not a warning).
3. Removing a skip = delete the row + enable the test in the same commit.
4. The registry is audited bidirectionally by `tools/check_test_hygiene.sh`:
   every executable `doctest::skip` cites one registered row, and every
   registered row has at least one live skip call.
5. The historical per-skip ticket files (TICKET-013, TICKET-012, TICKET-120,
   TICKET-007.i, TICKET-CABI-V2-VIDEO-CERT) are NOT re-materialized; git history
   remains the archive.

## Checkout update — 2026-09-24

- `tools/check_test_hygiene.sh` now checks the registry in both directions:
  every executable `doctest::skip` names exactly one registered id in the
  registered source file and declared `TEST_CASE`, each row has required
  fields and a live skip, deadlines must be ISO dates that have not passed,
  and duplicate/malformed rows fail. The registered self-test exercises
  valid, unregistered, stale, mismatched, malformed, duplicate, and expired
  rows.
- `SKIP-004` was retired with its test enabled. `HierarchyResolver::compute_depths`
  already marks every node in a parent cycle; the skip described code that had
  since landed.
- `SKIP-008/009/010` were retired with their tests enabled. The canonical text
  document compiler already handles single paragraphs, multiple paragraphs,
  and explicit empty ranges from consecutive newlines.
- `SKIP-005` was retired with its three tests enabled. The compiler now accepts
  static sources with `OrientAlongPath`; the existing evaluator emits the
  fallback warnings for previous tangent, point-of-interest, and base rotation.
- `SKIP-006` was retired with its test enabled. `stratified_jitter` hashes the
  center frame into its stateless seed, so repeated calls on one frame match
  and adjacent frames vary.
- `SKIP-007` is now retired with the test enabled in the video build and omitted
  entirely when video support is disabled. The video-enabled C ABI test passed
  locally (1 case / 7 assertions); its cancellation callback fires before
  rendering or output creation, so this verification did not use a GPU.
- `SKIP-001/002` were retired with actual overlapping-layer and blur-layer scenes.
  The determinism fixture's composite backend now performs a deterministic
  source-over blend; the blur scene exercises the effect-stack route and compares
  all five scheduler modes. The targeted tests pass locally.
- `SKIP-003` was retired with a SoftwareRenderer fixture: moving content creates
  dirty rectangles, 16-pixel tiles are asserted active, and six frame hashes
  match between Sequential and TbbAutomatic schedulers (27 assertions pass).
- Rebuilt `chronon3d_scene_tests` from the current tree and reran the enabled
  cycle-detection, five OrientAlongPath, frame-keyed temporal-jitter, and three
  TextRunBuilder paragraph cases: 10 cases / 72 assertions passed. The only
  CPU-side cases: 10 cases / 72 assertions passed. The registry is now empty;
  `tools/check_test_hygiene.sh` and its registered self-test pass.
