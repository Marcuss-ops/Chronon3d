# Ticket — SVG text advanced scopes and writing modes

**Status:** In progress
**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Owner:** Chronon3D C++ renderer

## TODO — cosa manca (agg. 2026-10-27)

- [ ] `textLength` annidati (parent/child) sull'intero testo shaped, incluse run styled separate; `spacing` e `spacingAndGlyphs`.
- [ ] `rotate` per-carattere continuo attraverso `<tspan>` annidati + comportamento sotto antenati ruotati/skew.
- [ ] `textLength` su `textPath` e certificazione della spaziatura con pixel coverage.
- [ ] Writing-mode verticale bounded oppure reject con diagnostica tipizzata.
- [ ] Pixel gate separati per target parent/child, run ruotati annidati, spaziatura textPath e repeat deterministici.

## Goal

Extend SVG text beyond the direct, bounded runs covered by the parent ticket,
while preserving the canonical shaping and rendering path.

## Remaining work

- Resolve nested parent and child `textLength` scopes over the complete shaped
  descendant text, including separate styled runs. Support `spacing` and
  `spacingAndGlyphs`; preserve anchor and coordinate-list behavior or reject
  combinations that cannot be represented faithfully.
- Carry per-character `rotate` indexing continuously through nested `<tspan>`
  runs and define its transform behavior under rotated or skewed ancestors.
- Apply `textLength` on `textPath` and certify letter/word spacing there with
  pixel coverage; preserve shaping across compatible adjacent clusters where
  path placement permits it.
- Add bounded vertical writing (`writing-mode` and `text-orientation`) or keep
  rejecting it with a typed importer diagnostic.
- Add pixel gates for parent and child `textLength` targets independently,
  rotated nested runs, textPath spacing, and deterministic repeats.

## Acceptance

- Nested parent and child targets are measured separately in raw pixels and
  match their requested advances within a documented raster tolerance.
- Unsupported combinations fail closed with stable diagnostics.
- Loader, C ABI and software raw-pixel gates pass on the same build.
- The parent SVG parity ticket remains limited to its documented software
  contract; strict-native Vulkan certification remains separate.
