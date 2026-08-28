# Historical baseline — `main@7eb5c2ba` (2026-07-06)

> This is historical evidence. It is **not** the v0.1 release SHA and must not
> be used as same-SHA certification for tag `v0.1`.
>
> Canonical v0.1 identity and certification rules: [`../RELEASE_V0_1_CONTRACT.md`](../RELEASE_V0_1_CONTRACT.md).

**11/11 PASS** — historical green baseline.

## Gate audit

| # | Gate | Esito | Note |
|---|---|---|---|
| 1 | `check_architecture_boundaries.sh` | ✅ PASS | Tutti i check statici rispettati. |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS | 15/15 assertions. |
| 3 | `check_software_renderer_boundary.sh` | ✅ PASS | I1-I5 rispettati. |
| 4 | `check_gitignored_dirs.sh` | ✅ PASS | Nessun abs-path leak. |
| 5 | `audit_software_renderer.sh` | ✅ PASS | Report generato. |
| 6 | `check_camera_architecture.sh` | ✅ PASS | 6/6 check. |
| 7 | `check_doc_sync.sh` | ✅ PASS | Doc-sync invariants hold. |
| 8 | `check_filename_drift.sh` | ✅ PASS* | Warn-mode; 87 drift findings. |
| 9 | `test_architectural.sh` | ✅ PASS | Static architecture-level rot: 0. |
| 10 | `install_consumer_test.sh` | ✅ PASS | Phase 1-4 PASS. |
| 11 | `check_backend_sanitization.py` | ✅ PASS | Tutti i check passati. |

## Scope

This baseline records the state of `main@7eb5c2ba` only. It does not certify
later commits, the current worktree, or tag `v0.1`.
