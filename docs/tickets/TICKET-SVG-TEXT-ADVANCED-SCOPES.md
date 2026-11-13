# Ticket — SVG text advanced scopes and writing modes

**Status:** PARTIAL — nested `textLength` with both adjustment modes is closed
locally under P0 of the parent parity ticket; nested per-character rotate,
`textLength` on `textPath`, and dedicated pixel gates remain open.
**Parent:** [TICKET-SVG-RENDER-PLAN.md](TICKET-SVG-RENDER-PLAN.md)
**Owner:** Chronon3D C++ renderer

## TODO — cosa manca (agg. 2026-10-28)

- [x] **`textLength` annidati (parent/child) + `spacing`/`spacingAndGlyphs`** — logica: risolvere i vincoli parent/child sull'intero testo shaped (inner-to-outer), incluse run styled separate; preservare anchor e coordinate list o rifiutare le combinazioni non rappresentabili. Dove: importer SVG text + shaping/layout HarfBuzz esistente. Gate: misura raw-pixel separata per target parent e child entro tolleranza documentata.
- [ ] **`rotate` per-carattere continuo attraverso `<tspan>` annidati** — logica: indicizzazione continua dei caratteri + comportamento definito sotto antenati ruotati/skew. Gate: pixel gate su run ruotati annidati.
- [ ] **`textLength` su `textPath`** — logica: applicare il vincolo anche sul path e certificare letter/word spacing con pixel coverage, preservando lo shaping attraverso cluster compatibili dove il posizionamento lo consente. Gate: pixel gate spaziatura textPath.
- [ ] **Writing-mode verticale bounded oppure reject tipizzato** — logica: scegliere una delle due semantiche dichiarate; mai accettazione silenziosa. Gate: test positivo verticale oppure diagnostica tipizzata stabile.
- [ ] **Pixel gate separati** — logica: target parent/child indipendenti, run ruotati annidati, spaziatura textPath, repeat deterministici. Gate: tutti i gate verdi sulla stessa build (loader + C ABI + software raw-pixel).

## Reconciliation — 2026-11-13

Nested `textLength` is implemented and covered by the parent ticket's raw
spacing gate, so it is no longer open work here. Vertical writing currently
fails closed with the stable importer diagnostic
`vertical SVG text writing modes are not supported by the bounded importer`;
the loader regression must assert this exact diagnostic before that bounded
alternative can be considered closed. Positive vertical layout and its
text-orientation/baseline semantics remain outside this reconciled ticket.

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
