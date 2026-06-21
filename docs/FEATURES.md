# Chronon3D — Feature Reference

Stato del repository: [`STATUS.md`](STATUS.md). Lavori da chiudere:
[`NEXT_STEPS.md`](NEXT_STEPS.md).

## Feature stabili presenti

- SVG Path V1: comandi `M/L/H/V/C/Q/Z`, incluse le forme relative.
- Text: FreeType, HarfBuzz, FriBidi, layout, auto-fit, overflow, preset, animazione per glifo, gradienti, stroke e glyph cache.
- Immagini, layer, mask, blend mode, camera 2.5D ed effetti software.
- Output video e telemetria controllati dalle opzioni di build.

Limiti noti:

- line breaking CJK non ancora basato su ICU;
- color emoji non supportate;
- import SVG limitato al primo path, senza supporto completo per styling, gruppi, trasformazioni e filtri.

La presenza di una feature non implica che l’intero repository sia release-ready. I blocker architetturali e di validazione sono descritti in `STATUS.md`.

## Expressions V2 — quarantena sperimentale

Expressions V2 è presente su `main`, ma non è una feature pubblica stabile.

| Superficie | Stato reale |
|---|---|
| Root | `experimental/expressions/` |
| Header | `experimental/expressions/include/chronon3d_experimental/expressions/v2/` |
| Sorgenti/test | Dentro `experimental/expressions/` |
| Build switch | `CHRONON3D_BUILD_EXPERIMENTAL=ON` |
| Default | Escluso |
| Install/export SDK | No |
| Integrazione produttiva | No |

`CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2` è soltanto una chiave CMake deprecata senza effetto.

Stato ticket:

- TICKET-003: chiuso.
- TICKET-004: chiuso.
- TICKET-005: follow-up separato su `keyframes()` e pulizia documentale.
- TICKET-EXP2-G3: migrazione reale da Path A a Path B.

La promozione richiede tutti gli otto gate di
[`EXPRESSIONS_V2_PROMOTION.md`](EXPRESSIONS_V2_PROMOTION.md): integrazione produttiva singola, determinismo, API documentata, benchmark, replacement map, deadline di rimozione e enforcement di un solo parser/VM/dependency graph.

Non includere `chronon3d_experimental/...` nel codice stabile prima della rimozione approvata della quarantena.
