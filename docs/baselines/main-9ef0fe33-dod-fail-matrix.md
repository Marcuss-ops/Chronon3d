# Pre-fix DoD FAIL Matrix — main @ 9ef0fe33

> Snapshot registrato: 2026-06-29
> Branch: `main`
> HEAD: `9ef0fe33` (post-fix path-correction su `tests/showcase/CMakeLists.txt:48-53`, pre-fix dei 3 root cause FAIL residui)
> Trigger: pin della matrice DoD 9-step a valle dei wave A1 + A3, in preparazione del prossimo wave di fix atomici.

## 1) Snapshot pre-fix

| Campo                     | Valore                                                       |
|---------------------------|--------------------------------------------------------------|
| `HEAD`                    | `9ef0fe33`                                                   |
| `HEAD_short`              | `9ef0fe33` (7 char)                                          |
| `origin/main`             | `9ef0fe33` (synced)                                          |
| Working tree              | pulito                                                       |
| Branch                    | `main` (NESSUN branch satellite)                             |
| Data snapshot             | 2026-06-29                                                   |
| Log matrix grezzo         | `/tmp/dod_c1_post_a3_final.log`                              |
| Script invocation         | `bash /tmp/dod_c1_verify.sh`                                 |
| Build dir                 | `/tmp/dod` (fresco)                                          |
| `vcpkg_bootstrap` root    | `<repo-root>/vcpkg_bootstrap`                |
| Doc-sync obbligatorio     | NO (nessun file `include/` o `src/` toccato in questo commit) |

## 2) Matrice DoD 9-step

Eseguito `/tmp/dod_c1_verify.sh` end-to-end al commit `9ef0fe33` (log: `/tmp/dod_c1_post_a3_final.log`).

| Step | Descrizione                              | Stato   | Motivo breve                                                                                                                                                |
|------|------------------------------------------|---------|-------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 1    | `1_configure` (cmake configure)          | ✅ PASS  | path-correction verde: prefix `cinematic/` → `showcase/cinematic/` su 5 source in `tests/showcase/CMakeLists.txt:48-53` (commit `9ef0fe33`). Nessun errore `Cannot find source file`. |
| 2    | `2_build_sdk_archive_merge`              | ❌ FAIL  | root cause separato: preprocessor error `expected initializer before '<' token` + `evaluate_fill_expression not a member of chronon3d` nei file `animated_value_evaluation.inl` (linee 38, 47, 142), `animated_value_bezier.inl` (linee 31, 92), `animated_value_roving.inl` (linea 28). |
| 3    | `3_ar_count_gt_1`                        | ⏭ SKIP  | cascade dallo Step 2 (archive `libchronon3d_sdk_impl.a` non buildato).                                                                                       |
| 4    | `4_nm_C_canaries`                        | ⏭ SKIP  | cascade dallo Step 2.                                                                                                                                       |
| 5    | `5_cmake_install`                        | ❌ FAIL  | cascade dallo Step 2 (richiede `libchronon3d_sdk_impl.a` non generato → impossibile installare).                                                              |
| 6    | `6_find_package`                         | ⏭ SKIP  | cascade.                                                                                                                                                     |
| 7    | `7_consumer_build`                       | ⏭ SKIP  | cascade.                                                                                                                                                     |
| 8    | `8_check_install_BOUNDARY_OK`            | ⏭ SKIP  | cascade.                                                                                                                                                     |
| 9    | `9_ghost_sweep_GHOST_OK`                 | ❌ FAIL  | ROT-2 sibling di Fix-2 (commit `81cdc738`): incomplete-type `chronon3d::runtime::RenderRuntime` nei callsites `src/animations/animated_value_evaluation.inl` + `src/animations/animated_value.hpp`. |

**TOTALE: 1 PASS / 3 FAIL / 5 SKIP — non-baseline-verificato.**

## 3) Dettaglio dei 3 root cause FAIL

### Root cause #1 — Step 2 (sdk_archive_merge build regression)

**Sintomo** (estratto da `/tmp/dod_c1_post_a3_final.log`):

```
error: expected initializer before '<' token
   include/chronon3d/animation/core/detail/animated_value_roving.inl:28
   include/chronon3d/animation/core/detail/animated_value_evaluation.inl:38
   include/chronon3d/animation/core/detail/animated_value_evaluation.inl:47
   include/chronon3d/animation/core/detail/animated_value_evaluation.inl:142
   include/chronon3d/animation/core/detail/animated_value_bezier.inl:31
   include/chronon3d/animation/core/detail/animated_value_bezier.inl:92
error: 'evaluate_fill_expression' is not a member of 'chronon3d'
   in src/animations/animated_value.hpp
error: 'evaluate_stroke_expression' is not a member of 'chronon3d'
   in src/animations/animated_value.hpp
```

**Diagnosi (preliminare):**

Il sed-fix in A1 (commit `73d5387e`) ha rimosso 116 shell-style `#`-only comment headers (102 con contenuto + 14 lone `#`) nei 5 file intestazione C++. Tuttavia, il preprocessor continua a segnalare `expected initializer before '<'` nelle stesse aree, sintomo tipico di `template<...>` non correttamente chiuso o di inline function definitions con signature male-extracted. Le due chiamate `evaluate_fill_expression` / `evaluate_stroke_expression` non risolvono come membri di `chronon3d` perché la relativa definizione nei `.inl` non viene istanziata (probabile problema di ODR o template specialization mancante).

**Scope:** 4 file in `include/chronon3d/animation/core/detail/` (`animated_value_roving.inl`, `animated_value_bezier.inl`, `animated_value_evaluation.inl`, `animated_value_expressions.hpp`) + 1 file in `src/animations/animated_value.hpp`.

**Relazione con Step 5:** Step 5 (`cmake_install`) richiede `src/libchronon3d_sdk_impl.a` che si builda in Step 2 → Step 5 è in cascata FAIL (Root cause #2 separato sotto).

### Root cause #2 — Step 5 install cascade

**Sintomo:** `5_cmake_install` FAIL perché manca `src/libchronon3d_sdk_impl.a`.

**Diagnosi:** questo Step **non ha un root cause proprio**: è un cascade diretto da Root cause #1 (Step 2 build). La chiusura di Root cause #1 chiuderà automaticamente questo Step. Non richiede commit dedicato.

### Root cause #3 — Step 9 (RenderRuntime incomplete-type ROT-2)

**Sintomo** (estratto da `/tmp/dod_c1_post_a3_final.log`):

```
error: incomplete type 'chronon3d::runtime::RenderRuntime' is not allowed
   in src/animations/animated_value_evaluation.inl
   in src/animations/animated_value.hpp
```

**Diagnosi:** ROT-2 è il **sibling di ROT-1 (Fix 2, commit `81cdc738`)**. Mentre Fix 2 ha aggiunto `#include <chronon3d/runtime/render_runtime.hpp>` a `src/render_graph/pipeline/scene_tile_execution.cpp` e `tile_execution_coordinator.cpp` (audit-comment block TICKET-038/TXT-00), ROT-2 è lo stesso pattern (forward-declaration di `RenderRuntime` seguita da invocazione di metodi senza incluso dell'header completo) nei callsites in area `animations/`. L'incomplete type è propagato dal compilatore da `animated_value.hpp` → `animated_value_evaluation.inl`.

**Scope:** 2 callsites in `src/animations/animated_value_evaluation.inl` + 2 callsite in `src/animations/animated_value.hpp`.

**Indipendenza da Root cause #1/#2:** ROOT cause **separato**. Anche se Step 2 fosse verde, Step 9 rimarrebbe FAIL fino alla fix specifica di include.

## 4) Commit-pianificati (Azione minima)

| ID    | Step target                  | SHA-pianificato           | File toccati                                                                                              | Azione minima                                                                                                                                  | Messaggio-tipo-tipo                                                                  |
|-------|------------------------------|---------------------------|-----------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------|
| CP-A  | Step 2 (sdk_archive_merge)   | (da assegnare post-fix)   | `include/chronon3d/animation/core/detail/animated_value_{roving,bezier,evaluation}.inl` + `animated_value_expressions.hpp` (4) + `src/animations/animated_value.hpp` (1) | Restauro template resolution: assicurare chiusura di `template<...>` nei punti segnalati dal preprocessor, verificare forwarding di `evaluate_fill_expression` / `evaluate_stroke_expression` su `animated_value.hpp`. | `fix(animations): complete template definitions in animated_value_*.inl (Step 2 sdk_archive_merge reg)` |
| CP-B  | Step 5 (install cascade)     | (nessuno)                 | (nessuno)                                                                                                  | Cascade: si chiude automaticamente quando CP-A è verde. Non richiede commit dedicato. | N/A                                                                                   |
| CP-C  | Step 9 (RenderRuntime ROT-2) | (da assegnare post-fix)   | `src/animations/animated_value_evaluation.inl` + `src/animations/animated_value.hpp` (2 callsites)     | Aggiungere `#include <chronon3d/runtime/render_runtime.hpp>` nei callsites che referenziano `runtime().executor()` (audit-comment block TICKET-038/TXT-00, replica Fix-2). | `fix(animations): render_runtime.hpp include for runtime().executor() ROT-2`         |
| CP-D  | re-run full matrix           | (da assegnare post-fix)   | `docs/baselines/main-<cp-d-hash>-dod-pass-matrix.md` (NEW)                                                 | Re-run di `/tmp/dod_c1_verify.sh` post CP-A + CP-C → atteso `9 PASS / 0 FAIL / 0 SKIP` o parimenti baseline-verificato. Documenta la baseline POST-fix. | `docs(baseline): pin post-fix DoD PASS matrix on main@<post-fix-sha>`                |

## 5) Catena dei commit rilevanti (su main → `9ef0fe33`)

```
9ef0fe33  fix(tests): correct cinematic fixture paths in tests/showcase/CMakeLists.txt add_executable   [A3]
73d5387e  fix(headers): convert shell-style # comments to // in 5 C++ headers (sed cascade Step 2 mitigation)  [A1]
81cdc738 [snap-sdk-m0-final]: 4-commit SDK chain closed (Fix 1 + Fix 2 + Fix 3 + Fix 4)
f27381a8  docs(agent-tasks): Phase-1.3.A pre-flight validation memo
cf3a2c30  fix(text): TEXT-RET-01 follow-up — persist retirement on main
203e2c47  refactor(install_consumer): slim orchestrator — source common.sh + invoke 4 phases
```

## 6) Cross-references

- `docs/STATUS.md` — snapshot corrente (riferimento: `81cdc738`); da aggiornare post CP-A + CP-C.
- `docs/NEXT_STEPS.md` — priority queue post A1+A3; sezione gap da riconsolidare.
- `docs/FOLLOWUP_TICKETS.md` — tracciamento ticket (TICKET-038 rot-1 precedent; eventuale ROT-2 ticket da aprire).
- `docs/baselines/main-446a60e2-baseline.md` — canonical reference (formato).
- `docs/baselines/main-9ef0fe33-dod-fail-matrix.md` — questo file.
- `AGENTS.md` — gate + commit discipline + per-commit hygiene.

---

Un'attività è **completata** soltanto quando codice, test, gate e documenti
riportano lo stesso stato. Questa baseline è pre-fix: registra fedelmente lo
stato FAIL corrente affinché la baseline POST-fix possa referenziarla con
diff-pointing.
