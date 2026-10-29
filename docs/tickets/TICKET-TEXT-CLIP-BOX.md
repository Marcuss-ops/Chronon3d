# TICKET-TEXT-CLIP-BOX — enforce `TextOverflow::Clip` for oversized text

**Stato:** CLOSED (2026-10-29; software renderer clips oversized ink in run-local box space)
**Priorità:** P3 — non blocking
**Owner:** text domain
**Reference test:** `tests/text/test_text_clip_oversized.cpp` (oversized visible control plus pixel-containment `CHECK`)

## Chiusura (2026-10-29)

- [x] **Enforcement renderer del clipping** — `composite_text_run` applica il clip al box locale prima della trasformazione, con copertura frazionaria ai bordi. Gate: `tests/text/test_text_clip_oversized.cpp` verifica che il render visibile ecceda il box e che `TextOverflow::Clip` mantenga pixel visibili solo dentro il box.
- [ ] **(Opzionale) Telemetria strutturata** — logica: promuovere `[text-vis] CLIP_DROPS_INK` a campo strutturato (node/layer id + box vs ink bbox) mantenendo il warn-once. Dove: `src/text/text_visibility_reporting.cpp` + layer telemetry. Gate: campo presente nei sidecar.

Verifica CPU: target `chronon3d_text_clip_oversized_tests` ricostruito e CTest
mirato passato (1/1); il controllo `Visible` supera il box, mentre `Clip`
mantiene il bbox dell'inchiostro dentro il box e conserva pixel visibili. La
telemetria strutturata resta facoltativa e fuori da questa chiusura.

## Semantica della limitation (machine-verified)

`TextOverflow::Clip` (`include/chronon3d/text/text_definition.hpp` + `text_layout_spec.hpp`) means
keep the authored font size and clip visible ink at the declared box. It does not enable auto-fit.
The software renderer applies the clip in run-local coordinates before the transformed composite.

Conseguenza osservabile:

- `verify_text_visibility()` (`src/text/text_visibility_audit.cpp` +
  `src/text/text_visibility_reporting.cpp`) rileva l'invariante di containment
  `clip_contains_visible_ink == false` (con tolleranza `kTextAuditBBoxTolerance`) e, nei build
  `CHRONON3D_BUILD_DIAGNOSTICS`, emette il diagnostico dedup:
  `[text-vis] CLIP_DROPS_INK node=<layer_id>`.
- Il warning è `spdlog::warn` diagnostic-only (dedup per `(node_id, kind)` via `WarnOnceDeduper`);
  in produzione non esiste segnale strutturato.
- `tests/text/test_text_clip_oversized.cpp` renders the same oversized string with `Visible` and
  `Clip`, requires the visible control to exceed the box, and checks pixel containment for `Clip`.

## Perché è una limitation, non un bug del renderer

Il contenimento ink-in-box richiede una decisione di policy di layout per il testo oversized:

- auto-fit (ADR-018 lineage) sul font size;
- ellipsis/truncation;
- clip documentato dell'ink al box dichiarato.

La decisione di prodotto è esplicita: `Clip` non riduce il font; l'auto-fit è attivato solo dal
flag dedicato previsto da ADR-018. Resta da implementare il taglio dei pixel al box nel renderer.

## Exit condition (per chiudere)

1. The text renderer clips visible ink to the authoring box when `TextOverflow::Clip` is active.
2. `test_text_clip_oversized.cpp` verifies pixel containment while also verifying that the
   authored font size remains unchanged.
4. (Opzionale) `[text-vis] CLIP_DROPS_INK` promosso a campo telemetry strutturato
   (node/layer id + box vs ink bbox), mantenendo il warn-once.

## Fuori confine

- `predicted_bbox`/compositor clip prediction (root cause diverso:
  `TICKET-TEXT-CLIP-PREDICTED-BBOX`, referenziato da `TICKET-TEXT-OVERSIZED-CLIP-WARN`, scheda rimossa il 2026-10-27 come duplicata).
- Qualsiasi modifica al percorso RenderingGen stabile (golden canary invariato).

## Cross-link

- Parent warning: `TICKET-TEXT-OVERSIZED-CLIP-WARN` (scheda rimossa il 2026-10-27 come duplicata: la diagnostica WARN vive in `tests/text/test_text_clip_oversized.cpp`)
- Regression lock: `tests/text/test_text_clip_oversized.cpp`
- Policy decision pointer: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`
