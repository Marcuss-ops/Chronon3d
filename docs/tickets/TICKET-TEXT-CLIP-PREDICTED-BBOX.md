# TICKET-TEXT-CLIP-PREDICTED-BBOX — text compositor `predicted_bbox` (Clip 06 diagnostic)

## Stato
PARTIAL

## Priorità
P0

## Problema
Il diagnostic test `tests/text_golden/text_clip/text_clip_bounds.cpp::Clip 06 TextClip DebugLayout Diagnostic 1920x1080` (HAMBURGER 180 pt, box=(1600,300), CenterAligned, position=(960,540,0), 1920×1080 canvas) mostra che `predicted_bbox` ritorna `(171, 472, 1251, 603)` (alphabet-bbox del render) mentre il bbox "vero" del layout (sotto `text_layout_debug=true` short-circuit) è `(0, 240, 1251, 603)`. Il delta è `+171` in x e `+232` in y. La hypothesis precedente "scratch surface" di `compute_text_run_visual_bounds` è stata **esclusa** dal verdict diagnostico:

> VERDICT: debug fixes vertical clipping -> bug is in predicted_bbox / compositor, NOT in scratch surface

Causa parziale identificata: `resolve_text_run_placement` (`src/render_graph/builder/graph_builder_coordinates.hpp`) in passato faceva strip+add di canvas-center producendo `translate(1920, 1080)` per layer canvas-centered, causando bbox a `(1131, 1014, 1919, 1079)` (testo off-canvas, 19-px sliver). Dopo gating del bake su `!item_was_implicit_centered`, il bbox è tornato in canvas `(171, 472, 1251, 603)`, ma i 171 px di offset in x e i 232 px in y persistono da una sorgente non ancora identificata (sospetti: `src/render_graph/pipeline/scene_refresh.cpp` override di `placement.matrix` post-`resolve_text_run_placement`; oppure secondo applicazione di canvas-center a valle del resolver).

## Evidenza
- `tests/text_golden/text_clip/text_clip_bounds.cpp::Clip 06` (TEST_CASE line 401; VERDICT line 446).
- Misurazioni alpha-bbox su output 1920×1080:
  - NORMAL (pre-fix strip+add): `(1131, 1014, 1919, 1079)` — off-canvas.
  - NORMAL (post-fix gating `!item_was_implicit_centered`): `(171, 472, 1251, 603)` — in-canvas ma clippato.
  - DEBUG (`text_layout_debug=true`): `(0, 240, 1251, 603)` — ink position reale.
- Code-reviewer feedback (3 turni): root cause non completamente diagnosticata; necessario logging `placement.matrix` + local bbox da `compute_text_run_visual_bounds` direttamente in `predicted_bbox()` per tracciare la matrice effettiva prima del prossimo tentativo di fix.

## Impatto
- Blocca la certificazione golden del cluster `text_golden/text_clip/text_clip_bounds.cpp` (Clip 01–06 tutti coinvolti).
- Possibile root cause di parte delle 17/24 scene test failures (`TICKET-120`).
- Impatta il rilascio V0.1 perché il `predicted_bbox` guida `tile_pruning` + `clip_rect` del compositor — senza fix corretto, layer canvas-centered finiscono clippati.

## Confine
- Solo il path `predicted_bbox` del `TextRunNode` + il resolver `resolve_text_run_placement` + eventuali override in `src/render_graph/pipeline/scene_refresh.cpp` / executor.
- Escluso: refactor di `compute_text_run_visual_bounds` (chiuso da `TICKET-TEXT-CLIP-ASCENT` 2026-07-08) — quella math è OK, il bug è a valle del layout.

## Soluzione accettabile
1. Stampare `placement.matrix`, `m_matrix_override`, e local bbox da `compute_text_run_world_bbox` direttamente in `TextRunNode::predicted_bbox()` (behind `SPDLOG_DEBUG` o env-gated).
2. Tracciare l'eventuale override di `placement.matrix` in `src/render_graph/pipeline/scene_refresh.cpp` + `src/render_graph/executor/node_runner.cpp` + `src/render_graph/executor/tile_pruning.cpp`.
3. Applicare il fix minimo al resolver/executor che identifica la doppia-applicazione di canvas-center residua (post-fix `!item_was_implicit_centered` ha ridotto il delta da 1890 px a 403 px totali).
4. Verificare che `Clip 06` torni al bbox `(0, 240, 1251, 603)` (uguale a DEBUG) e che Clip 01–05 goldens (vedi `TICKET-TEXT-CLIP-GOLDENS-01-05`) possano essere seedati correttamente.

## Criteri di accettazione
- `Clip 06` NORMAL bbox = `(0, 240, 1251, 603)` entro 1 px di tolleranza.
- Zero regressioni sui 35/35 AE-parity goldens + su Clip 01–05 (post-seed, vedi `TICKET-TEXT-CLIP-GOLDENS-01-05`).
- Code-reviewer APPROVED con comment esplicito "root cause identified + fix is targeted, not shotgun".
- Build verification su host funzionante (VPS attuale unfit per vcpkg glm/magic_enum + tmpfs quota — vedi `AGENTS.md` §honesty).

## Linkage
- Test diagnostico: `tests/text_golden/text_clip/text_clip_bounds.cpp` (Clip 06).
- Resolver sospetto: `src/render_graph/builder/graph_builder_coordinates.hpp::resolve_text_run_placement` (fix parziale già applicato: gating del bake su `!item_was_implicit_centered`).
- Pipeline sospetta: `src/render_graph/pipeline/scene_refresh.cpp`, `src/render_graph/executor/node_runner.cpp`, `src/render_graph/executor/tile_pruning.cpp`.
- Lineage: `TICKET-TEXT-CLIP-ASCENT` (chiuso 2026-07-08, math OK) → questo ticket (root cause a valle del layout).
- Ticket fratello: `TICKET-TEXT-CLIP-GOLDENS-01-05` (Clip 01–05 goldens da seedare post-fix).
- Stato corrente: [`docs/CURRENT_STATUS.md`](../CURRENT_STATUS.md) §Active Blockers + [`docs/FOLLOWUP_TICKETS.md`](../FOLLOWUP_TICKETS.md) §Open Blockers.
