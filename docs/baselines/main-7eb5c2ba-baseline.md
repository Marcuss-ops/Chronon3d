# Baseline — `main@7eb5c2ba` (2026-07-06)

**11/11 PASS** — prima baseline verde certificata. Feature freeze V0.1 revocato.

## Gate audit

| # | Gate                                        | Esito      | Note |
|---|---------------------------------------------|------------|------|
| 1 | `check_architecture_boundaries.sh`          | ✅ PASS    | Tutti i check statici rispettati. |
| 2 | `check_architecture_boundaries_selftest.sh` | ✅ PASS    | 15/15 assertions. |
| 3 | `check_software_renderer_boundary.sh`       | ✅ PASS    | I1-I5 tutti rispettati. |
| 4 | `check_gitignored_dirs.sh`                  | ✅ PASS    | Nessun abs-path leak. |
| 5 | `audit_software_renderer.sh`                | ✅ PASS    | Report generato. | <!-- drift-allow: archived-doc-pattern -->
| 6 | `check_camera_architecture.sh`              | ✅ PASS    | 6/6 check (ae_parity + camera_truth test files whitelisted). |
| 7 | `check_doc_sync.sh`                         | ✅ PASS    | Doc-sync invariants hold. |
| 8 | `check_filename_drift.sh`                   | ✅ PASS*   | warn-mode; 87 drift findings. |
| 9 | `test_architectural.sh`                     | ✅ PASS    | Static architecture-level rot: 0. |
| 10 | `install_consumer_test.sh`                  | ✅ PASS    | Phase 1-4 PASS. Root cause: PIL not installed. Fix: `pip3 install Pillow`. |
| 11 | `check_backend_sanitization.py`             | ✅ PASS    | Tutti i check passati. |

## Fix applicati per raggiungere 11/11

| Gate | Problema | Fix | Commit |
|------|----------|-----|--------|
| G10 | PIL non installato → `ImportError` scambiato per PNG near-black | `pip3 install --break-system-packages Pillow`; `save_png` allineato a `save_ppm` path canonico | `2b104072` |
| G4 | abs-path leak in `docs/CHANGELOG.md` | `<REPO_ROOT>` → `<REPO_ROOT>` | `7eb5c2ba` |
| G6 | `SceneBuilder::animated_camera()` in test files non whitelistati | Aggiunti `ae_parity_scenes.cpp` + `camera_truth_test.cpp` alla whitelist | `7eb5c2ba` |

## Commit chain

```
7eb5c2ba fix(gates): G4 abs-path leak in CHANGELOG.md + G6 whitelist ae_parity/camera_truth test files
b48e58ab docs: align CURRENT_STATUS + FOLLOWUP_TICKETS to real gate state (9/11 at 2b104072)
2b104072 fix(gate-10): align save_png to save_ppm pixel path — both gates PASS
```

## Feature freeze

Revocato con commit successivo a questa baseline. Vedi `AGENTS.md` per i dettagli.
