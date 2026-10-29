# Ticket — SVG text advanced scopes and writing modes

**Status:** In progress
**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Owner:** Chronon3D C++ renderer

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **`textLength` annidati (parent/child) + `spacing`/`spacingAndGlyphs`** — logica: risolvere i vincoli parent/child sull'intero testo shaped (inner-to-outer), incluse run styled separate; preservare anchor e coordinate list o rifiutare le combinazioni non rappresentabili. Dove: importer SVG text + shaping/layout HarfBuzz esistente. Gate: misura raw-pixel separata per target parent e child entro tolleranza documentata.
- [ ] **`rotate` per-carattere continuo attraverso `<tspan>` annidati** — logica: indicizzazione continua dei caratteri + comportamento definito sotto antenati ruotati/skew. Gate: pixel gate su run ruotati annidati.
- [ ] **`textLength` su `textPath`** — logica: applicare il vincolo anche sul path e certificare letter/word spacing con pixel coverage, preservando lo shaping attraverso cluster compatibili dove il posizionamento lo consente. Gate: pixel gate spaziatura textPath.
- [ ] **Writing-mode verticale bounded oppure reject tipizzato** — logica: scegliere una delle due semantiche dichiarate; mai accettazione silenziosa. Gate: test positivo verticale oppure diagnostica tipizzata stabile.
- [ ] **Pixel gate separati** — logica: target parent/child indipendenti, run ruotati annidati, spaziatura textPath, repeat deterministici. Gate: tutti i gate verdi sulla stessa build (loader + C ABI + software raw-pixel).

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
