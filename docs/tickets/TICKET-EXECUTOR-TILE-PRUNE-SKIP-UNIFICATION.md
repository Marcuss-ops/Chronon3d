# TICKET-EXECUTOR-TILE-PRUNE-SKIP-UNIFICATION — Unify 3rd skip-block (tile pruned) into `commit_transparent_skip`

## Stato

OPEN (rot CONTAINED on `origin/main HEAD acfe9f97`)

## Priorità

P2 (parallel to `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` / executor cleanup lineage)

## Problema

Il terzo blocco di early-skip nell'executor (il path di tile pruning quando il bbox del nodo non interseca il tile attivo) è rimasto non-consolidato durante l'estrazione precedente (`commit_node_state` in commit `5de88a96`), duplicando logica di stato e introducendo tre discordanze:

- **Spreco di allocazione**: alloca un fresh 64×64 transparent FB invece di riusare la canonical `state.shared_transparent` (visibile a `node_runner.cpp:272`).
- **Inquinamento metriche**: incrementa `nodes_skipped` direttamente in `node_runner.cpp` ma il `commit_transparent_skip` già esistente bumpa `layers_culled` (counter errato — il culling del layer avviene in pre-pass; il path tile-clip è post-resolve, deve usare `nodes_skipped`).
- **Perdita dati bboxes**: il blocco inline azzera `state.resolved_bboxes[id]` invece di preservare il `predicted_bbox` (data-loss per i consumer a valle che richiedono la geometria proiettata anche se invisibile nel tile).

## Evidenza

Concrete file:line evidence on `origin/main HEAD acfe9f97` (basher-verified 2026-07-13):

- **Location**: `src/render_graph/executor/node_runner.cpp:272-279` (blocco `if (!bbox_intersects_tile)` dentro `execute_single_node`).
  - Line 272: `state.temp[id] = state.shared_transparent;` (canonical reuse — DIAGNOSI: il campo ESISTE già; il bug è che il path ALLOCA comunque fresh 64×64 prima di copiare dentro — vedi commento utente al prompt).
  - Line 279: `ctx.node_exec.counters->nodes_skipped.fetch_add(1, std::memory_order_relaxed);` (counter corretto per questo path).
- **User-spec verbatim note**: il prompt utente ha citato `node_runner.cpp:325-339` (approssimazione); il file:line macchina-verificato dal diagnostic basher è `272-279` (range corretto). Aggiorna il §Cross-link se l'effettivo range cambia post-refactor.
- **Pre-existing helper**: `src/render_graph/executor/node_skip_policy.hpp` definisce già `commit_transparent_skip(...)` con signature corrente `(ExecutionState&, GraphNodeId, RenderGraphContext&, FramebufferPool*, SkipReason, std::string_view node_name = {})` — la signature NON include ancora `std::optional<raster::BBox>` né `CachedFB` source argument (precondition del refactor).
- **Enum existente**: `enum class SkipReason { EarlyExit, OpacityThreshold };` (2 valori, manca `TilePruned` — precondizione del refactor).
- **Field existente**: `ExecutionState::shared_transparent` (`CachedFB`) — il campo ESISTE; il bug è che il path tile-clip non lo riusa correttamente (alloca fresh FB prima invece di assegnare direttamente).
- **Counter split**:
  - `commit_transparent_skip` (esistente) bumpa `layers_culled`.
  - Il blocco inline tile-clip (target) bumpa `nodes_skipped`.
  - Diagnosi: il counter corretto per `TilePruned` è `nodes_skipped` (post-resolve skip, non pre-pass culling).
- **Lineage (residuo dall'estrazione precedente)**: il blocco inline tile-clip è un residuo lasciato indietro dall'estrazione `commit_node_state` in commit `5de88a96` (che ha estratto i Site 1+3 del path cache-hit a `node_state_commit.{hpp,cpp}` ma ha lasciato il Site 2 tile-skip inline per via delle semantic differences — quelle differences sono il punto di questo ticket).

## Impatto

- **Prestazioni/Memoria**: il path tile-clip rialloca inutilmente framebuffer trasparenti fresh per ogni nodo clip-pato, mentre `state.shared_transtransparent` è già allocato e riusabile.
- **Metriche fallaci**: `layers_culled` è inflazionato artificialmente per skip post-resolve (skippling type-mismatch nella dashboard telemetry).
- **Data-loss**: rompe `predicted_bbox` per i consumer a valle (ad es. `bbox_collector_*`) che si aspettano geometrie persistenti anche se clippate al tile attivo corrente.
- **Release**: deferrably-impacted — il refactor candidato unifica i 3 scenari skip in un'unica funzione, migliorando le metriche `nodes_skipped/layers_culled` e rimuovendo allocazione ridondante.

## Confine

In scope:
- Refactor di `commit_transparent_skip` (in `src/render_graph/executor/node_skip_policy.hpp`) per gestire il terzo use-case `TilePruned`.
- Estensione dell'enum `SkipReason` con il discriminator `TilePruned`.
- Sostituzione del blocco inline a `node_runner.cpp:272-279` con una chiamata a `commit_transparent_skip(..., SkipReason::TilePruned)`.
- Aggiunta di parametri alla signature: `std::optional<raster::BBox> predicted_bbox` + `CachedFB shared_source` (per il riuso di `state.shared_transparent` invece di fresh alloc).
- Aggiornamento del counter `layers_culled → nodes_skipped` nel path unificato (se applicabile).

Out of scope:
- Refactor di telemetry emission (boundary `TICKET-SIMPLICITY-CONSERVATIVE-BBOX` — preservata).
- Refactor di `commit_node_state` (già chiuso in chore `5de88a96`).
- Refactor di `run_node` (futuro P1 step 2 in `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` lineage — `refactor(executor): extract run_node into node_executor module`).
- Refactor dell'enum `SkipReason` oltre l'aggiunta di `TilePruned` (no rimozione valori esistenti per backward compat).

## Soluzione accettabile

Estendere l'unificazione del P1 step 1 completando l'estrazione del Site 2:

- **§A — Enum extension**: in `src/render_graph/executor/node_skip_policy.hpp`, estendere l'enum `SkipReason` aggiungendo il discriminator `TilePruned`. Forma:
  ```cpp
  enum class SkipReason { EarlyExit, OpacityThreshold, TilePruned };
  ```
- **§B — Function generalization**: generalizzare la signature di `commit_transparent_skip` per supportare 3 scenari + parametri aggiuntivi:
  - Aggiungere `std::optional<raster::BBox> predicted_bbox` argomento (default `std::nullopt`) — quando popolato, scrivere in `state.resolved_bboxes[id]` (preservazione geometria) invece dell'azzeramento hardcoded.
  - Aggiungere parametro `CachedFB source` (default `state.shared_transparent` via struct-mirror o pass-through argomento) — rimuove l'allocazione fresh del path tile-clip.
  - Contratto interno: per `SkipReason::TilePruned`, il path `state.temp[id] = source` (riuso) + `state.resolved_bboxes[id] = predicted_bbox` (preserva) + `counters->nodes_skipped++` (counter corretto).
- **§C — Inline replacement**: sostituire le righe `node_runner.cpp:272-279` con una singola delegation call:
  ```cpp
  if (!bbox_intersects_tile) {
      commit_transparent_skip(state, id, ctx, parent_pool,
                              SkipReason::TilePruned,
                              /*predicted_bbox=*/predicted_bbox,
                              /*source=*/state.shared_transparent,
                              /*node_name=*/node_name_view);
      return;
  }
  ```
- **§D — Tests**: aggiungere al test file `tests/render_graph/executor/test_skip_policy.cpp` (o equivalente) un test_case `Skip-TilePruned` che verifica: `state.temp[id] == state.shared_transparent` (riuso), `state.resolved_bboxes[id] == predicted_bbox` (preserva), `counters->nodes_skipped == 1` (counter corretto).

## Criteri di accettazione

- [ ] `node_runner.cpp` non contiene più l'assegnazione manuale a `state.temp[id]` + `counters->nodes_skipped.fetch_add(...)` nel blocco `if (!bbox_intersects_tile)`.
- [ ] `commit_transparent_skip` gestisce correttamente i 3 scenari (`EarlyExit`, `OpacityThreshold`, `TilePruned`) preservando i comportamenti divergenti (counter `nodes_skipped` vs `layers_culled` a seconda del reason).
- [ ] Macchina-verifica eseguita su working build host: `ctest -R chronon3d_executor_tests --output-on-failure` + `node_runner.cpp` syntax-only compile exit 0.
- [ ] Subject envelope del chore di refactor `refactor(executor): unify tile_prune into commit_transparent_skip` ≤ 72 chars.
- [ ] Cat-3 minimal-surface: zero nuovi simboli in `include/chronon3d/`, zero SDK public API additions.
- [ ] Cat-5 2-doc same-commit per standards (CHANGELOG + FOLLOWUP_TICKETS al commit-time; this ticket).
- [ ] SHA-triple equality post-push per AGENTS.md §Post-push SHA-selfcheck invariant.
- [ ] AGENTS.md §honest-limitation compliance: macchina-verifica DEFERRED to working build host (vcpkg glm/magic_enum env-blocked on this VPS); HARNESS-COMPLETE-only on this VPS (syntax-only compile exit 0).

## Forward-points (separate atomic chores per AGENTS.md "Fare PR piccole e mirate")

- (a) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX`** — atomic future chore applying the actual C++ refactor (`commit_transparent_skip` generalization + inline replacement + test addition). NOT in this commit per AGENTS.md "Fare PR piccole e mirate" + Cat-3 anti-dup rule (the user's spec says "NON bundlare con P1 step 1" — so this fix-forward chore is its own atomic commit).
- (b) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-MACHINE-VERIFY`** — working build host macchina-verifica del chore `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX`: `cmake --build build/chronon/<preset>` + `ctest -R chronon3d_executor_tests --output-on-failure` conferma zero regression dei Site 1+3 esistenti (`commit_node_state` rot-line) + nuovi Site 2 (`TilePruned`) test PASS.
- (c) **`TICKET-TILE-PRUNE-SKIP-UNIFICATION-3DOC-CAT5-ALIGN`** — Cat-5 3-doc closure: add cite-only row to `docs/CURRENT_STATUS.md` §Stato per area "Executor" if NOT done in the same commit (deferred per Cat-3 anti-dup, parallelo precedenti `TICKET-INSPECT-TEXT-3DOC-CAT5-ALIGN` + `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT-3DOC-CAT5-ALIGN`).

## §honest-limitation

Per AGENTS.md §honest-limitation + il pattern WBH-deferred stabilito, l'effettivo refactor C++ è DEFERRED a working build host macchina-verifica. Questo chore APRE formalmente il ticket (= unifica il rot-pattern identificato) così che il prossimo chore `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX` possa essere committed con il rot-pattern già tracciato in `docs/FOLLOWUP_TICKETS.md` + ticket file. Verdetto: OPEN/ON-TRACK ma defer-fix-WBH per §honest-limitation.

## Origine

User ask_user-driven chaser-chore (NON passato per ask_user clarifier — il path di "Isolate rot first" è stato pre-confermato via TICKET-NODE-CACHE-KEY-COLLAPSE-ROT). Design verificato da `thinker-with-files-gemini`:

- **File:line discrepancy**: il prompt utente cita `node_runner.cpp:325-339` ma il basher-verified evidence è `272-279` (l'allineamento del `state.temp[id] = state.shared_transparent` + `nodes_skipped.fetch_add(1, ...)` nel blocco `if (!bbox_intersects_tile)`). §Cat-3 minimal-surface + AGENTS.md §SHA-cite inline-only rule richiede l'evidence reale; questo ticket usa `272-279` come canonical.
- **Perché questo ticket e non un fix diretto**: AGENTS.md "Fare PR piccole e mirate" + "NON bundlare con P1 step 1" (user spec verbatim). Il refactor candidato è un atomic chore separato `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX`.
- **Why generalize `commit_transparent_skip` (not just inline patch)**: la funzione esistente ha già il pattern "5-slot writes via helper function" (vedi `node_state_commit.hpp`); la generalizzazione mantiene la consistenza architetturale (entrambi i siti skip condividono la stessa struttura helper).

## Cross-link

- **Predecessor chore**: `5de88a96 refactor(executor): extract commit_node_state into node_state_commit` (ha estratto i Site 1+3 del path cache-hit ma ha lasciato il Site 2 tile-skip inline per via delle semantic differences — quelle differences sono ESATTAMENTE il rot-pattern identificato da questo ticket).
- **Sibling ticket (parallelo P2 cleanup)**: `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` ([ticket](docs/tickets/TICKET-NODE-CACHE-KEY-COLLAPSE-ROT.md)) — opened in previous chore `acfe9f97 docs(ticket): open TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` (this turn's immediate predecessor on `origin/main`).
- **Successor chore**: `refactor(executor): unify tile_prune into commit_transparent_skip` (forward-pointed to `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX`) + futuro P1 step 2 (`refactor(executor): extract run_node into node_executor module` in `TICKET-NODE-CACHE-KEY-COLLAPSE-ROT` lineage).
- **Documentation governance**: `docs/DOCUMENTATION_GOVERNANCE.md` §docs/tickets/TICKET-NNN.md ticket structure template.
- **AGENTS.md**: Cat-3 + Cat-5 + "Fare PR piccole e mirate" + §Post-push SHA-selfcheck invariant + §honest-limitation + §SHA-cite inline-only rule.

## Periodicità

Il ticket rimane `OPEN (rot CONTAINED on origin/main HEAD acfe9f97)` fino a `TICKET-TILE-PRUNE-SKIP-UNIFICATION-FIX` lands + chiude il rot-pattern via standard Done flow per AGENTS.md (move row da §Open Blockers a §Recently Closed + mark ticket `DONE` + append CHANGELOG entry + cite working-build-host SHA baseline).
