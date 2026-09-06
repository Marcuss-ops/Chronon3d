# TICKET-TEXT-CLIP-BOX — KNOWN LIMITATION: `TextOverflow::Clip` is not enforced for oversized text

**Stato:** OPEN (limitation formalizzata 2026-09-06; nessun change di codice)
**Priorità:** P3 — non blocking
**Owner:** text domain
**Reference test:** `tests/text/test_text_clip_oversized.cpp` (4 assertion `WARN`, commento inline che cita questo ticket)

## Semantica della limitation (machine-verified)

`TextOverflow::Clip` (`include/chronon3d/text/text_definition.hpp` + `text_layout_spec.hpp`) è la
policy di default dichiarata a livello di PreLayout (reflow), ma **non esiste enforcement a valle**:
quando un layer di testo supera la propria box dichiarata (testo troppo lungo/largo), il layout
overflowa la box senza correzione né auto-fit e il renderer NON clippa l'ink al box.

Conseguenza osservabile:

- `verify_text_visibility()` (`src/text/text_visibility_audit.cpp` +
  `src/text/text_visibility_reporting.cpp`) rileva l'invariante di containment
  `clip_contains_visible_ink == false` (con tolleranza `kTextAuditBBoxTolerance`) e, nei build
  `CHRONON3D_BUILD_DIAGNOSTICS`, emette il diagnostico dedup:
  `[text-vis] CLIP_DROPS_INK node=<layer_id>`.
- Il warning è `spdlog::warn` diagnostic-only (dedup per `(node_id, kind)` via `WarnOnceDeduper`);
  in produzione non esiste segnale strutturato.
- `tests/text/test_text_clip_oversized.cpp` (TICKET-FALSE-GREEN-TEST-AUDIT, anti-false-green clip
  regression lock) usa 4 `WARN` (non `CHECK`) per la bbox contenuta: il test resta verde perché la
  limitation è dichiarata, non nascosta.

## Perché è una limitation, non un bug del renderer

Il contenimento ink-in-box richiede una decisione di policy di layout per il testo oversized:

- auto-fit (ADR-018 lineage) sul font size;
- ellipsis/truncation;
- clip documentato dell'ink al box dichiarato.

La scelta è **policy di prodotto per i layer preset** (title_centered / kinetic_word /
entity_card / stat / quote di PipelineGen), non una decisione del core engine: vedi
`TICKET-TEXT-OVERSIZED-CLIP-WARN` §"Definire la policy di output" e il pointer product-validation
in `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md` (fuori confine core).

## Exit condition (per chiudere)

1. Policy esplicita documentata per i layer preset (auto-fit / ellipsis / clip) — decisione di
   prodotto registrata (RenderingGen/PipelineGen side).
2. Il motore clippa (o riadatta) l'ink alla box dichiarata quando `TextOverflow::Clip` è attivo
   OPPURE la policy scelta al punto 1 è implementata e certificata.
3. `test_text_clip_oversized.cpp`: le 4 `WARN` diventano `CHECK` (o la policy scelta rende il
   containment non più l'invariante atteso e il test viene aggiornato in modo esplicito).
4. (Opzionale) `[text-vis] CLIP_DROPS_INK` promosso a campo telemetry strutturato
   (node/layer id + box vs ink bbox), mantenendo il warn-once.

## Fuori confine

- `predicted_bbox`/compositor clip prediction (root cause diverso:
  `TICKET-TEXT-CLIP-PREDICTED-BBOX`, referenziato da `TICKET-TEXT-OVERSIZED-CLIP-WARN`).
- Qualsiasi modifica al percorso RenderingGen stabile (golden canary invariato).

## Cross-link

- Parent warning: `docs/tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md`
- Regression lock: `tests/text/test_text_clip_oversized.cpp`
- Policy decision pointer: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`
