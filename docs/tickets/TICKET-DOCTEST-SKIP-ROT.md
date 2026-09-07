# TICKET-DOCTEST-SKIP-ROT — Skip registry (single authority for doctest::skip)

Status: OPEN (registry authority)
Owner: testing-infra
Created: 2026-09-07

## Purpose

Every `doctest::skip(...)` in `tests/` must cite this ticket plus the row id
below. The archive folder was removed on 2026-09-05 ("git history is the only
archive"), which orphaned the ticket references inside skip reasons
(TICKET-013, TICKET-012, TICKET-120, TICKET-007.i, TICKET-CABI-V2-VIDEO-CERT).
This file is the single live authority for skip rot: one row per skipped test,
with owner, deadline and reactivation condition. A skip without a row here is
a lint failure; a row past its deadline blocks the certification gate (CI FAIL,
not a warning).

## Registry

| Row | Test (file :: TEST_CASE prefix) | Reason | Owner | Deadline | Reactivation condition |
|---|---|---|---|---|---|
| SKIP-001 | `tests/render_graph/executor/test_scheduler_determinism.cpp` :: "WP1 PR 1.4 — composite scene (2 overlapping layers)" | FakeBackend `composite_layer` is a no-op; latent executor SIGSEGV on next-stage `dst` read under no-op blend. Needs deterministic blit in FakeBackend OR a real SoftwareBackend fixture. | chronon3d-owners | 2026-09-30 | FakeBackend::composite_layer performs a deterministic blit, or fixture wires SoftwareBackend |
| SKIP-002 | `tests/render_graph/executor/test_scheduler_determinism.cpp` :: "WP1 PR 1.4 — one effect stack (blur)" | Same root as SKIP-001: `FakeBackend::apply_blur` no-op makes the layer-mode blur path unsafe. | chronon3d-owners | 2026-09-30 | Same as SKIP-001 |
| SKIP-003 | `tests/render_graph/executor/test_scheduler_determinism.cpp` :: "WP1 PR 1.4 — tile execution" | Tile-execution determinism needs a higher-level fixture (dirty rects + tile-enabled settings) — TICKET-012 scope. | chronon3d-owners | 2026-09-30 | Tile fixture lands (dirty rects populated + `tile_execution_enabled`) |
| SKIP-004 | `tests/scene/transform_hierarchy_tests.cpp` :: "HierarchyResolver: cycle detection" | Cycle-detection path not yet implemented/locked; see test body. | scene-core | 2026-09-30 | HierarchyResolver cycle detection implemented + test enabled |
| SKIP-005 | `tests/scene/camera/test_orient_along_path.cpp` :: 3 cases (tangent warning / keep-rotation) | Orphaned TICKET-120 scope: OrientAlongPath warning semantics not yet locked. | camera-v1 | 2026-09-30 | OrientAlongPath warning contract implemented |
| SKIP-006 | `tests/scene/camera/test_temporal_samples_pr1.cpp` :: "PR1: temporal::generate_temporal_samples — frame-keyed jitter differs" | Orphaned TICKET-007.i: frame-keyed jitter determinism check deferred. | temporal-core | 2026-09-30 | Jitter sampling contract implemented |
| SKIP-007 | `tests/c_abi/test_c_api_v2.cpp` :: "C ABI v2 cancellation certification requires video support" | Orphaned TICKET-CABI-V2-VIDEO-CERT: C ABI cancellation certification requires the video-enabled build host. | sdk-abi | 2026-09-30 | Video-enabled certification host available in CI |

## Rules

1. New `doctest::skip(...)` sites MUST add a row here and cite
   `TICKET-DOCTEST-SKIP-ROT SKIP-NNN` in the skip reason.
2. Rows past their deadline fail the certification aggregator (P0 blocker,
   not a warning).
3. Removing a skip = delete the row + enable the test in the same commit.
4. The historical per-skip ticket files (TICKET-013, TICKET-012, TICKET-120,
   TICKET-007.i, TICKET-CABI-V2-VIDEO-CERT) are NOT re-materialized; their
   content is subsumed by the rows above (git history remains the archive).
