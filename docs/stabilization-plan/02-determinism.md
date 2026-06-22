# Determinismo scheduler e rendering

> **Fonte canonica del dettaglio:** [`docs/02-determinism.md`](../02-determinism.md) — contratto di riproducibilità pixel-perfect, 4 superfici (Serial, TBB, Composite, Tile), test e stato per-§.

## Stato sintetico

| Superficie | Stato |
|---|---|
| Serial path | 🟢 Done (PR 6.9) |
| TBB path | 🟢 Done (PR 6.9) |
| Composite path | 🟢 Done (PR 6.8) |
| Tile path | 🟢 Done (PR 6.1) |

## Ancora aperto

- [ ] Sostituire tutti i sentinel con golden hash numerici reali.
- [ ] Risolvere il blocker toolchain/backend che impedisce la cattura pulita.
- [ ] Eseguire da checkout e build puliti i test di acquisizione.
- [ ] Copiare i valori osservati nelle costanti `kRefBaseline*`.
- [ ] Rimuovere il comportamento dormant basato su `kUncapturedSentinel`.
- [ ] Aggiungere un FakeBackend con compositing deterministico (TICKET-013).
- [ ] Riattivare ogni test ancora legato a TICKET-013.

## Completato quando

Tutti i percorsi producono lo stesso output numerico atteso e nessuna costante golden usa ancora `0xDEADBEEFDEADBEEF` o un altro placeholder.
