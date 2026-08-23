# Baseline — `main@1078ab46` (2026-07-04)

## Gate Audit: 10/11 PASS

| # | Gate | Esito | Note |
|---|------|-------|------|
| 1 | `check_architecture_boundaries.sh` | ✅ PASS | 15/15 check, 2 advisory |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS | 15/15 assertions |
| 3 | `check_software_renderer_boundary.sh` | ✅ PASS | I1-I5 ok |
| 4 | `check_gitignored_dirs.sh` | ✅ PASS | |
| 5 | `audit_software_renderer.sh` | ✅ PASS | | <!-- drift-allow: archived-doc-pattern -->
| 6 | `check_camera_architecture.sh` | ✅ PASS | 6/6 check |
| 7 | `check_doc_sync.sh` | ✅ PASS | 0 failures, 0 warnings |
| 8 | `check_filename_drift.sh` | ✅ PASS | 82 drift findings |
| 9 | `test_architectural.sh` | ✅ PASS | |
| 10 | `install_consumer_test.sh` | ❌ FAIL | Phase 1-3 PASS, Phase 4 render black (pre-existing) |
| 11 | `check_backend_sanitization.py` | ✅ PASS | |

**Feature freeze:** Ancora attivo (richiede 11/11 PASS). Gate #10 da risolvere.

**Commit snapshot:** P1 matrix (item #15), FOLLOWUP housekeeping (item #14), sw_sidecar fix, camera_v1 headers manifest fix.
