# TICKET-TEXT-CLIP-BOX — KNOWN LIMITATION: `TextOverflow::Clip` is not enforced for oversized text

**Stato:** OPEN (product policy clarified 2026-09-24; renderer enforcement remains open)
**Priorità:** P3 — non blocking
**Owner:** text domain
**Reference test:** `tests/text/test_text_clip_oversized.cpp` (sets authored
`font_size=200`; checks that ink is visible and overflows the box; pixel
containment is `WARN` while renderer clipping remains open. It does not assert
the effective font size after layout.)

## Semantica della limitation (machine-verified)

`TextOverflow::Clip` (`include/chronon3d/text/text_definition.hpp` + `text_layout_spec.hpp`) means
keep the authored font size and clip visible ink at the declared box. It does not enable auto-fit.
The current renderer still does not enforce that pixel clip: oversized text can overflow its box.

Conseguenza osservabile:

- `verify_text_visibility()` (`src/text/text_visibility_audit.cpp` +
  `src/text/text_visibility_reporting.cpp`) rileva l'invariante di containment
  `clip_contains_visible_ink == false` (con tolleranza `kTextAuditBBoxTolerance`) e, nei build
  `CHRONON3D_BUILD_DIAGNOSTICS`, emette il diagnostico dedup:
  `[text-vis] CLIP_DROPS_INK node=<layer_id>`.
- Il warning è `spdlog::warn` diagnostic-only (dedup per `(node_id, kind)` via `WarnOnceDeduper`);
  in produzione non esiste segnale strutturato.
- `tests/text/test_text_clip_oversized.cpp` supplies `font_size=200` for a 200×100
  box and observes visible ink outside it. The source does not currently assert
  the effective font size after layout; pixel containment remains `WARN` until
  renderer clipping is implemented.

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
  `TICKET-TEXT-CLIP-PREDICTED-BBOX`, referenziato da `TICKET-TEXT-OVERSIZED-CLIP-WARN`).
- Qualsiasi modifica al percorso RenderingGen stabile (golden canary invariato).

## Current software reproduction — 2026-10-11

The `linux-video-test` target `chronon3d_text_clip_oversized_tests` built and
ran on dirty checkout `e8e06f5b3`: **1/1 case, 4/4 hard assertions passed**.
Its `WARN` reported the rendered alpha bbox `(895,531)-(1024,1079)` against the
declared box ending at `y=590`; doctest therefore returned success while
preserving this known clipping failure. The run also emitted the
`resolve_engine: no FontEngine provided` fallback warning, so this result is a
software-path reproduction, not a font-specific golden. The renderer clipping
exit condition remains OPEN.

## Renderer boundary audit — 2026-10-11

The canonical box and policy are available on `TextRunShape::layout_spec`
(`TextLayoutSpec::box` and `overflow`). The software text path does not consume
that policy at its final composite: `composite_text_run()` composites the full
scratch image through `composite_bl_image_transformed()` without passing a clip
state. The native Vulkan text route consumes the fixed-size
`runtime::TextRunDynamic` payload; its current shader record has no clip box or
overflow flag. Therefore clipping only the glyph raster surface in software
would leave Vulkan behavior divergent. A complete fix needs a shared
transformed-box contract and enforcement in both the software composite and
Vulkan run payload/shaders. This is a source audit, not a behavior fix; the
ticket remains OPEN and no GPU result is claimed.

## Cross-link

- Parent warning: `docs/tickets/TICKET-TEXT-OVERSIZED-CLIP-WARN.md`
- Regression lock: `tests/text/test_text_clip_oversized.cpp`
- Policy decision pointer: `../refactored/docs/product-validation/PRODUCT-VALIDATION-AGGREGATOR.md`
