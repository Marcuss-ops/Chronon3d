# TICKET-TEXT-OVERSIZED-CLIP-WARN — warning Chronon di clipping testo oversized (NON BLOCKING)

## Stato
OPEN

## Priorità
P3 — **NON BLOCKING**: nessun impatto sul percorso RenderingGen stabile né sul golden canary (rendering reale PASS, artifact verificato, replay idempotente verde). Ticket dedicato per isolare la WARN dal lavoro RenderingGen, che resta congelato.

## Problema
Durante la validazione E2E del golden canary (PipelineGen → queue → RenderingGen → `chronon3d_cli` reale), il worker log ha mostrato una warning di **clipping di testo oversized**: quando un layer di testo supera il proprio box (testo troppo lungo/largo per la box dichiarata o per il preset), l'ink renderizzato esce dal box e viene clippato, e Chronon emette il diagnostico:

```
[text-vis] CLIP_DROPS_INK node=<layer_id>
```

La warning è emessa da `verify_text_visibility()` quando l'invariante di **containment** `clip_contains_visible_ink` fallisce: l'alpha-bbox renderizzato NON è contenuto (con tolleranza `kTextAuditBBoxTolerance`) nel clip rect. È il sintomo della **KNOWN LIMITATION `TICKET-TEXT-CLIP-BOX`**: `TextOverflow::Clip` non è ancora enforced per testo oversized — il layout overflowa la box senza correzione né auto-fit (vedi `tests/text/test_text_clip_oversized.cpp`, le 4 assertion sono `WARN`, non `CHECK`).

Stato attuale del problema:
1. Il warning è **diagnostico-only** (`spdlog::warn`, dedup per `(node_id, kind)` via `WarnOnceDeduper`), gated da `CHRONON3D_BUILD_DIAGNOSTICS`: in produzione non c'è alcun segnale strutturato.
2. Non esiste una **policy esplicita** per i layer preset generati da PipelineGen (`title_centered`, `kinetic_word`, `entity_card`, stat/quote): il testo oversized viene clippato silenziosamente (dal punto di vista del job) — l'unico segnale è la WARN nel worker log.
3. Il reference `TICKET-TEXT-CLIP-BOX` citato in `test_text_clip_oversized.cpp` **non ha una scheda ticket**: la limitation è documentata solo come commento nel test.

## Evidenza
- `src/text/text_visibility_reporting.cpp` — emissione `spdlog::warn("[text-vis] CLIP_DROPS_INK node={}", nm)` quando `audit.clip_contains_visible_ink == false`.
- `src/text/text_visibility_audit.cpp` — invariante `clip_contains_visible_ink` via containment `expand(clip, kTextAuditBBoxTolerance) ⊇ rendered_alpha_bbox` (non semplice intersezione, chiude `TICKET-TEXT-CLIP-19-PIXEL-SLIVER`).
- `src/render_graph/nodes/TextRunNode.cpp` — audit F1.E post-render con `clip_rect = predicted_r` (il compositor usa `predicted_bbox` come clip per i TextRun).
- `tests/text/test_text_clip_oversized.cpp` — KNOWN LIMITATION `TICKET-TEXT-CLIP-BOX`: "TextOverflow::Clip is not yet enforced for oversized text. Keep as WARN until the engine clips ink to the declared box." (4 assertion `WARN` su bbox contenuta).
- Golden canary E2E: `RenderingGen/infra/e2e/run-golden-overlay.sh` — la WARN è comparsa nel worker log durante il render reale (job completato comunque, artifact SHA verificato).
- `include/chronon3d/text/text_definition.hpp` + `include/chronon3d/text/text_layout_spec.hpp` — campo `overflow` (Clip default) con fase PreLayout (reflow), nessuna enforcement a valle per testo oversized.

## Impatto
- **Zero** sul percorso stabile: render PASS, artifact corretto, replay idempotente senza rerender, PostgreSQL certificato.
- Rischio qualitativo: un testo troncato può passare inosservato nei job (nessun segnale strutturato in produzione, solo WARN di log diagnostica).
- `TICKET-TEXT-CLIP-BOX` rimane una limitation orfana (citata ma senza scheda): va formalizzata o chiusa.

## Confine
- **Solo lato Chronon** (diagnostica + policy di testo): warning semantics, telemetria opzionale, eventuale enforcement/policy futura.
- **ESCLUSO**: qualsiasi modifica al percorso RenderingGen stabile (worker, queue, objectstore, `run-golden-overlay.sh`, payload golden congelato). Il canary resta byte-identico.
- Escluso: fix di `predicted_bbox` (root cause diverso, vedi `TICKET-TEXT-CLIP-PREDICTED-BBOX`).

## Soluzione accettabile
1. **Formalizzare `TICKET-TEXT-CLIP-BOX`**: aprire la scheda della limitation (o chiuderla in questo ticket) con la semantica attuale: per testo oversized, `TextOverflow::Clip` non contiene l'ink nel box.
2. **Definire la policy di output** per i layer preset (title_centered / kinetic_word / entity_card / stat / quote): auto-fit (ADR-018), ellipsis, o clip documentato — decisione di prodotto, non tecnica.
3. **Opzionale**: promuovere la WARN a telemetria strutturata (campo nel report / telemetry record con node/layer id + box vs ink bbox), mantenendo il warn-once.
4. Documentare la WARN nel worker log come segnale atteso (non error) per job con testo lungo.

## Criteri di accettazione
- `TICKET-TEXT-CLIP-BOX` formalizzata (scheda aperta) o chiusa con policy documentata.
- Policy esplicita documentata per i preset PipelineGen (auto-fit / ellipsis / clip) — decisione registrata.
- Nessuna modifica ai file RenderingGen; golden canary invariato (stesso SHA, stesse certification).
- (Opzionale) WARN strutturata disponibile in telemetria.

## Linkage
- **Test/regression separato**: [`TICKET-TEXT-OVERSIZED-CLIP-REGRESSION-TEST`](TICKET-TEXT-OVERSIZED-CLIP-REGRESSION-TEST.md) (questo ticket è il parent: warning; l'altro è il lock testuale).
- Known limitation: `TICKET-TEXT-CLIP-BOX` (reference in `tests/text/test_text_clip_oversized.cpp:81`, scheda da formalizzare).
- Sibling (root cause diverso, NON confuso con questo): [`TICKET-TEXT-CLIP-PREDICTED-BBOX`](TICKET-TEXT-CLIP-PREDICTED-BBOX.md) (P0, divergence predicted_bbox) + [`TICKET-TEXT-CLIP-GOLDENS-01-05`](TICKET-TEXT-CLIP-GOLDENS-01-05.md) (P1, golden seed).
- Golden canary: `RenderingGen/infra/e2e/run-golden-overlay.sh` (percorso stabile, non toccare).
