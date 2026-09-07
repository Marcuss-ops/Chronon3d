# Compiler Demolition Waves — Wave 1–4

Status: `ACTIVE`

Obiettivo: lasciare una sola authority per execution, job normalization,
compiler output e graph lowering, senza introdurre una seconda pipeline
temporanea. Ogni riga diventa `DONE` solo quando il codice, i caller e il gate
sono coerenti.

## Todo / acceptance criteria

- [x] Wave 1 — `command_plan_executor` rimosso da production e test helper.
- [x] Wave 1 — `CommandPlan` resta solo come IR/contratto dati e passa da
  `GraphExecutor`.
- [x] Wave 1 — authority gate senza `execute_command_plan` o
  `command_plan_executor` in `src/`, `include/` e `apps/`.
- [x] Wave 2 — `RenderJobRequest` e `normalize_render_job()` sono l’unica
  normalizzazione backend/codec/range per il percorso render-plan.
- [x] Wave 2 — CLI e IPC invocano lo stesso entrypoint request-based, senza la
  lista legacy di parametri e senza il passaggio IPC policy → overrides.
- [x] Wave 2 — `RenderPlanState` resta solo DTO CLI; la policy non è più
  risolta da questo adapter e il job executor riceve il risultato normalizzato.
- [x] Wave 2 — il percorso render-plan trasporta un solo `CompiledProgram`;
  la `CompiledComposition` legacy viene materializzata una sola volta nel
  resolver per gli executor ancora non migrati.
- [x] Wave 3 — `CompiledProgram` immutable con topology, parameters e
  fingerprint esposti dalla compiler boundary.
- [ ] Wave 3 — il render-plan runtime esegue direttamente topology e parameter
  programs di `CompiledProgram`, senza materializzare `PreparedRenderPlan` /
  `CompiledComposition` come payload dell’executor.
- [x] Wave 4 — target CMake separati di render-plan/graph builder/graph
  compiler assorbiti nel target `chronon3d_compiler`.
- [x] Wave 4 — `graph_compile.hpp` pubblico e free facade rimossi; il graph
  lowering passa da un seam interno al compiler.
- [x] Wave 4 — wrapper `GraphBuilder::build` e relativo translation unit
  eliminati; i test usano direttamente la pipeline dei compiler pass.
- [ ] Wave 4 — census finale: nessun caller production indipendente di
  `build_graph`, `compile_frame_graph` o `compile_render_plan`.

## Demolition debt

- Owner: compiler/runtime maintainers.
- Reason: migrazione da render-plan/graph authorities separate verso il
  compiler root e `CompiledProgram`.
- Exit condition: i tre compatibility payload/entrypoint sopra elencati non
  compaiono più nel production execution path e i gate di boundary passano.
- Equivalence gate: compiler boundary, graph executor authority, CLI/IPC
  request parity, targeted render-graph tests e build `linux-fast-dev`.
- Removal scope: legacy CLI adapter, public graph facade, obsolete CMake
  sources/targets e compatibility payload non più referenziati.
- Status: `ACTIVE`.

## Verification snapshot

- Build `linux-fast-dev` dei target compiler, graph pipeline, CLI render e test
  boundary: `PASS`.
- Gate architetturale: `PASS` (122/122 regole).
- Gate compiler/executor/runtime ownership: `PASS`.
- `compiler_boundary`, `render_job_contract` e il test isolato tile: `PASS`.
- Suite completa `render_graph`: `FAIL` pre-esistente/non correlato alle aree
  modificate; il test `Pinned absolute TextRun stays centered across animation
  boundary` fallisce con 4 assertion anche in esecuzione isolata. Non viene
  marcata verde.
