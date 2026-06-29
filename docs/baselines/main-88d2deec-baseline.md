# Baseline Preliminare — main @ 88d2deec

> Recorded: 2026-06-29
> Branch: `main`
> HEAD: `88d2deec` (presets: split CMakePresets.json into 6 include files + remove 12 legacy presets + orphan linux buildPreset)
> Trigger: esecuzione locale manuale degli 11 gate architetturali su commit candidato (Feature Freeze attivo).
> Commit di registrazione: `f111abfe` (docs + followup tickets reconciliation atop 88d2deec).

## Riepilogo

| # | Gate | Script | Exit | Esito |
|---|------|--------|------|-------|
| 1 | Architecture boundaries | `tools/check_architecture_boundaries.sh` | 0 | ✅ PASS |
| 2 | Architecture boundaries selftest | `tools/check_architecture_boundaries_selftest.sh` | 1 | ❌ FAIL |
| 3 | Software renderer boundary | `tools/check_software_renderer_boundary.sh` | 1 | ❌ FAIL |
| 4 | Gitignored dirs | `tools/check_gitignored_dirs.sh` | 1 | ❌ FAIL |
| 5 | Software renderer audit | `tools/audit_software_renderer.sh` | 1 | ❌ FAIL |
| 6 | Camera architecture | `tools/check_camera_architecture.sh` | 0 | ✅ PASS |
| 7 | Doc sync | `tools/check_doc_sync.sh` | 0 | ✅ PASS |
| 8 | Filename drift | `tools/check_filename_drift.sh` | 1 | ❌ FAIL |
| 9 | Test architectural | `tools/test_architectural.sh` | 1 | ❌ FAIL |
| 10 | Install consumer test | `tools/install_consumer_test.sh` | 2 | ❌ FAIL |
| 11 | Backend sanitization | `tools/check_backend_sanitization.py` | 0 | ✅ PASS |

**Verdetto: 4/11 PASS — baseline NON VERDE** 🔴

---

## Dettaglio FAIL

### Gate 2 — `check_architecture_boundaries_selftest.sh`

- **Exit code**: 1
- **Esito**: 10 test failures, 12 passes
- **Natura**: Il selftest dello script di architecture boundaries ha fallimenti interni (probabili hardcoding o riferimenti stale negli expected values).

### Gate 3 — `check_software_renderer_boundary.sh`

- **Exit code**: 1
- **Violazioni**:
  - **I2**: header `software_renderer.hpp` LOC = 203 > target 200 (target R4)
  - **I3**: non-local includes = 7 > target 6 (target R4)
- **Bug script**: syntax error su line 97 (`command substitution: syntax error near unexpected token '&&'`)

### Gate 4 — `check_gitignored_dirs.sh`

- **Exit code**: 1
- **Violazioni**: 2 directory violations (0 glob, 0 file)
- **Bug script**: line 116 (`.gitignore: command not found`), line 178 (`[: too many arguments`)

### Gate 5 — `audit_software_renderer.sh`

- **Exit code**: 1
- **Natura**: Lo script abortisce durante la generazione del report. Causa: `grep -E '^#include "[^"]+"'` restituisce exit 1 (zero match) in combinazione con `set -o pipefail` + `set -e` sulla pipeline `grep ... | wc -l`. La variabile `LOCAL_INC` manca del pattern `|| true` presente su altre grep analoghe nello stesso script.
- **Effetto**: Nessun report scritto; script interrotto prima di `echo "Report written: $OUT"`.

### Gate 8 — `check_filename_drift.sh`

- **Exit code**: 1
- **Violazioni**: 182 drift findings in strict mode. File citati in sorgenti, test, build system o documentazione che non sono presenti su disco.

### Gate 9 — `test_architectural.sh`

- **Exit code**: 1
- **Violazioni**:
  - **Section 1**: Stale `CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` directive in `CMakeLists.txt`.
  - **Section 2**: `static std::unordered_map` e `static std::vector` vietati in multipli file sorgente e test.
  - **Section 3**: `doctest::skip()` senza metadata `TICKET-XXX` richiesto in `test_motion_blur_torture_pr1.cpp`.

### Gate 10 — `install_consumer_test.sh`

- **Exit code**: 2
- **Errore**: `CMake Error: Could not read presets from /home/pierone/Pyt/Chronon3d: Inherited preset is unreachable from preset's file`
- **Natura**: Dopo lo split Phase-1.3 dei CMakePresets in 6 file (`cmake/presets/{base,development,ci,release,experimental,profiling}.json`), i preset ereditati (es. `linux-ci` in `ci.json` che eredita da `base` e `base-linux` in `base.json`) non vengono risolti correttamente dalla toolchain CMake quando chiamata dallo script.

---

## Gate PASS

### Gate 1 — `check_architecture_boundaries.sh`

- **Exit code**: 0
- **Esito**: "All architecture boundary checks PASSED!"
- **Note**: Check individuali per SoftwareRenderer boundaries, CMake module registry e vcpkg dep parity sono segnati come `FAIL (advisory)` ma il gate complessivo è PASS.

### Gate 6 — `check_camera_architecture.sh`

- **Exit code**: 0
- **Esito**: Tutti i 6 check passati.

### Gate 7 — `check_doc_sync.sh`

- **Exit code**: 0
- **Esito**: "OK: doc-sync invariants hold"

### Gate 11 — `check_backend_sanitization.py`

- **Exit code**: 0
- **Esito**: "SUCCESS: All sanitization checks passed!"

---

## Cross-references

- `AGENTS.md` — Feature Freeze attivo; definizione baseline verde (11/11 PASS).
- `docs/CURRENT_STATUS.md` — snapshot allineato a `88d2deec`.
- `docs/ROADMAP.md` — M0 baseline verificata.
- `docs/RELEASE_GATE.md` — 11 gate listati.
- `docs/FOLLOWUP_TICKETS.md` — blocker aperti correlati ai gate falliti.

## Priority queue (baseline verde)

1. **Gate 10** (install_consumer_test) — Fix della catena di include dei CMakePresets; bloccante per certificazione SDK consumer.
2. **Gate 5** (audit_software_renderer) — Fix del bug nello script (`grep` senza `|| true`); sblocca il report R1.
3. **Gate 4** (check_gitignored_dirs) — Fix dei bug di script + risoluzione delle 2 directory violations.
4. **Gate 3** (software_renderer_boundary) — Ridurre header LOC ≤ 200 e non-local includes ≤ 6; fix del syntax error su line 97.
5. **Gate 9** (test_architectural) — Rimuovere direttiva stale CMake, sostituire static containers vietati, aggiungere TICKET metadata ai `doctest::skip()`.
6. **Gate 8** (filename_drift) — Risolvere i 182 drift findings.
7. **Gate 2** (arch_boundaries_selftest) — Sistemare i 10 test falliti nel selftest.
