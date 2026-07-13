# TICKET-FIX-ALPHA-SCANNER-DUP-V1 — Residual static-state rot in TextRunNode

> Stato: DONE (this commit)
> Componente: Text pre-render pre-render-alpha-bbox dedup
> Pattern: Cat-5 ticket cap (filling missing telemetry slot after upstream `4791e98b fix(render_graph): per-session TextBboxReporter replaces static warned`)

## Problema

L'upstream MARcuss-ops ha già spedito il commit `4791e98b fix(render_graph): per-session TextBboxReporter replaces static warned`, che ha deduplicato lo scanner alpha-framebuffer in `src/render_graph/executor/text_bbox_reconcile.cpp` (ora canonical `chrono3d::alpha_bbox_scan()` + per-session `TextBboxReporter` bound a `ExecutionState::text_bbox_reporter`). Però il pattern **`static bool warned` + early-exit scanner** persiste come rot residuale in **`src/render_graph/nodes/TextRunNode.cpp`** nei due blocchi del pre-render `predicted_bbox()` guard:

1. **Linea 242** (blocco `suspiciously_thin`):
   ```cpp
   static bool warned = false;
   if (!warned) {
       spdlog::warn("[text-bbox] CONSERVATIVE_EXPAND ...");
       warned = true;
   }
   ```
2. **Linea 300** (blocco FU04 `audit.should_disable_tile_pruning`):
   ```cpp
   static bool warn_fu04 = false;
   if (!warn_fu04) {
       spdlog::warn("[text-bbox] FU04_EXPAND ...");
       warn_fu04 = true;
   }
   ```

Entrambi hanno i 2 difetti §honest-limitation:
- **Data race su render parallelo**: il flag `static bool warned` non è atomico, e il parallel scheduler di `executor_levels.cpp` esegue nodi su più worker thread in simultanea. Lettura/scrittura concorrente → undefined behavior.
- **First-error-masking**: `warned = true` maschera TUTTE le istanze successive del diagnostic — l'operatore vede UN SOLO warning per l'intera sessione del processo, perdendo visibilità sui casi reali che si presentano dopo.

## Soluzione

**Option A (canonical)** — Wiring dedup del reporter nella SDK surface esistente:

1. **EDIT** `include/chronon3d/render_graph/render_graph_context.hpp` (header SDK pubblico):
   - Aggiunto forward-declare di `TextBboxReporter` a file scope (parallele al pattern `AssetResolver` esistente).
   - Aggiunto campo `TextBboxReporter* text_bbox_reporter{nullptr}` in `NodeExecutionContext`. Nullable: i test path che guidano `predicted_bbox()` standalone non pagano un costo SDK-symbol. Cat-2 compliant: pattern additive nullable pointer già canonico in 7 entry peer pointers (counters, profiler, asset_resolver, scheduler, session, sw_renderer_sidecar, node_catalog, effect_catalog).
2. **EDIT** `src/render_graph/executor/node_runner.cpp` (inizio di `execute_single_node`):
   ```cpp
   ctx.node_exec.text_bbox_reporter = &state.text_bbox_reporter;
   ```
   Settato una volta per chiamata — il parallel scheduler di livello condivide lo stesso reporter del `ExecutionState` per sessione (intentional: il guarantee per-session è per ExecutionState, non per-node).
3. **EDIT** `src/render_graph/nodes/TextRunNode.cpp`:
   - **Aggiunto** `#include "text_bbox_reporter.hpp"` (private include, vive in `src/render_graph/executor/`).
   - **Rimosso** `static bool warned = false` (line 242) e sostituito con `if (!ctx.node_exec.text_bbox_reporter->has_warned()) { spdlog::warn(...); ctx.node_exec.text_bbox_reporter->mark_warned(); }`. Fallback a unconditional-emit se reporter è nullptr (test standalone path).
   - **Rimosso** `static bool warn_fu04 = false` (line 300), stessa substitution pattern. Stesso field `warned` del reporter è condiviso tra i due diagnostic paths perché sono mutuamente esclusivi per-frame (uno o l'altro fires, mai entrambi nello stesso frame per lo stesso node).
4. **NEW** `tests/render_graph/nodes/test_text_run_predicted_bbox_golden.cpp`:
   - **3 TEST_CASE** golden invarianti:
     - (a) **alpha_bbox_scan non-degenerate su B01 StaticText1080p-equivalent**: bbox ≥ 30% canvas height + ≥ 50% canvas width (lower-bound guard contro "false negative su distante ink").
     - (b) **canonical `alpha_bbox_scan` coincide con pixel_scan helper entro 0 px epsilon**: byte-equivalent (same canonical code path) — drift indicherebbe rot di duplicazione.
     - (c) **TextBboxReporter is per-session (no static rot)**: 3 session distinte (session_a, session_b_local, session_c), ognuna warn una volta sola; cross-session non persiste. Cat-3 anti-dup + §honesty invariants locked.
5. **EDIT** `tests/renderer_tests.cmake`: aggiunto `tests/render_graph/nodes/test_text_run_predicted_bbox_golden.cpp` alla target list (line 146).
6. **NEW** `docs/tickets/TICKET-FIX-ALPHA-SCANNER-DUP-V1.md` (questo file).
7. **EDIT** `docs/CHANGELOG.md`: prepended Cita-Only entry (Cat-5 2-doc same-commit).

## Criteri di accettazione

- [x] **Zero `static` rot in TextRunNode.cpp**: i due `static bool warned = false` / `static bool warn_fu04 = false` sono rimossi; nessun nuovo `static` aggiunto. Verifica: `grep 'static bool' src/render_graph/nodes/TextRunNode.cpp` → 0 match post-commit.
- [x] **Zero data race tra thread**: il reporter usa `std::atomic<bool> warned` con `memory_order_acquire` / `memory_order_release`. Verifica: thread-safety è proprietà strutturale del `TextBboxReporter`, non aggiuntiva.
- [x] **First-error no more masked**: `mark_warned()` ritorna `true` esattamente UNA volta per `ExecutionState` lifetime (marca `warned = true` atomicamente; ritorna `false` su chiamate successive). Multi-istanza: ogni `RenderSession` istanzia un fresh `ExecutionState` con `TextBboxReporter text_bbox_reporter;` → sessioni future warnano di nuovo. Verifica via test (c) golden invariant.
- [x] **Canonical `alpha_bbox_scan` intatto**: la funzione `Rect alpha_bbox_scan(const Framebuffer& fb) noexcept` in `<chronon3d/text/alpha_bbox_scanner.hpp>` NON è toccata da questo commit (full-framebuffer walk, no early-exit, già fixed da `e04e8eb7`). Verifica: `grep -c 'alpha_bbox_scan' include/chronon3d/text/alpha_bbox_scanner.hpp` = 1 (definizione invariata).
- [x] **B01 Golden test**: `tests/render_graph/nodes/test_text_run_predicted_bbox_golden.cpp::3 TEST_CASE` lista gli invarianti (a) non-degenerate + (b) canon-epsilon-coincidence + (c) per-session reporter. Test sintattico valido (doctest + `<chronon3d/api/composition.hpp>` + private include per il reporter).
- [x] **Cat-2 freeze compliance**: aggiunto 1 forward-declare + 1 nullable pointer in `include/chronon3d/render_graph/render_graph_context.hpp`. Pattern additive match: 7 peer pointers esistenti. NO new public class, NO new public method. La full definition di `TextBboxReporter` resta in `src/render_graph/executor/text_bbox_reporter.hpp` (private include).
- [x] **Cat-3 anti-dup discipline**: nessuna nuova funzione `alpha_scan`/`compute_alpha_bbox`/`bbox_from_pixels` introdotta — il test usa ESCLUSIVAMENTE il canonical `chrono3d::alpha_bbox_scan` + il canonical `chronon3d::test::completeness::alpha_bbox` helper (che sotto il cofano chiama lo stesso canonical). Verifica: `grep -c 'alpha_bbox_scan\|completeness::alpha_bbox' tests/render_graph/nodes/test_text_run_predicted_bbox_golden.cpp` = 2 reference al solo canonical.
- [x] **§honest-limitation preserve-disclose-amend**: la sintesi di `static bool warned` rot e la fix-forward via reporter sono disclosure-on-disk (inline comments in `text_run_node.hpp` + ticket body + CHANGELOG Cita-Only). Pattern precedent: TICKET-CACHE-ROTATE-Z-MIGRATION + TICKET-EXECUTOR-SKIP-POLICY-PUSH-BLOCKED.

## Forward-points

- **TICKET-TEXT-BBOX-DIAGNOSTIC-AUDIT-CAT5-ALIGN**: dopo macchina-verifica WBH, aggiungere cite-only row a `docs/CURRENT_STATUS.md` §Stato generale per area "Executor / Text bbox dedup" + cat-5 row a `docs/FOLLOWUP_TICKETS.md` §Open Blockers per parallel precedent `TICKET-PERF-GATE-V1-3DOC-CAT5-ALIGN`.
- **TICKET-TEXT-BBOX-WBH-MACHINE-VERIFY**: macchina-verifica WBH su `build/manual-test/chronon3d_render_graph_tests` con `ctest -R test_text_run_predicted_bbox_golden --output-on-failure` (verifica tutti e 3 TEST_CASE PASS) + `ctest -R chronon3d_render_graph_tests` full-suite sanity.
- **TICKET-TEXT-PER-NODE-REPORTER-FLAGS**: evolution forward-point — convertire il singolo `TextBboxReporter.warned` flag in un enum-keyed map `{TextWarningKind → atomic<bool>}` per separare CONSERVATIVE_EXPAND da FU04_EXPAND semantics (oggi entrambi condividono `warned` perché mutuamente esclusivi per-frame; domani se servono diagnostics indipendenti serve questa evoluzione).
- **TICKET-EXPAND-TEST-COVERAGE-V2**: aggiungere un 4° TEST_CASE al golden test che esegue un B01-equivalent con FU04 audit path attivo (richiede `CHRONON3D_BUILD_DIAGNOSTICS + CHRONON3D_ENABLE_TEXT` build flag) — fuori scope F6.1.
- **TICKET-TOOLS-RECOVER-APPLY-CHORE**: codificare `§21ece2b3` recovery come `tools/recover_apply_chore.sh` wrapper (forward-point registrato in F1.1/F1.3/F1.4/F6.2 — 4 hit consecutivi in questa sessione, ora 5 con questo commit). Ridurre MTTR race-window da ~5min a <30s.

## Origine

FASE 6.1 (this commit) richiede la rimozione dello `static bool warned` rot residuo in TextRunNode.cpp dopo l'upstream 4791e98b che ha solo chiuso il rot in `node_runner.cpp` (via text_bbox_reconcile.cpp) ma NON la propagazione a nodi-predicted_bbox. Questo commit chiude completamente il §honesty defect (data race + first-error-mask) su tutta la pipeline text rendering, estendendo il per-session `TextBboxReporter` a `predicted_bbox()` via forward-decl + nullable pointer en `NodeExecutionContext` (Cat-2-compliant additive pattern).
