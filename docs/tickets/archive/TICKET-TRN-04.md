# TICKET-TRN-04 — Text transition semantics cleanup

## Stato: DONE

## Problema
Le transizioni testuali in `AnimatedTextDocument` avevano tre questioni aperte:

1. `SourceTextTransition::Cut` non aveva una semantica chiara rispetto a `Hold`.
2. Il crossfade (`DissolveLayouts`) doveva usare alpha complementari.
3. Mancava una decisione formale su `DissolveLayouts` vs `MorphLayouts`.

## Soluzione accettata

### 1. Cut = Hold al boundary
`Cut` è **semanticamente identico a `Hold`** fino al prossimo keyframe boundary, poi switcha istantaneamente. La differenza è solo intenzionale/editoriale: `Cut` segnala al renderer che non serve tenere in vita il layout uscente oltre il boundary.

Implementazione: `src/text/animated_text_document.cpp` tratta `Cut` come `Hold` fino al boundary.

### 2. Alpha complementari in DissolveLayouts
Durante `DissolveLayouts`:

```text
incoming alpha = mix
outgoing alpha = 1 - mix
```

Implementazione: `src/backends/software/processors/text_run/text_run_processor/raster.cpp` applica `shape.dissolve_mix` al lato incoming e `1.0f - shape.dissolve_mix` al lato outgoing (main tier loop e dissolve side).

### 3. DissolveLayouts vs Morph
Entrambi restano transizioni distinte:

| Transizione | Scopo | Tipo |
|-------------|-------|------|
| `DissolveLayouts` | Crossfade alpha tra due layout completi | Alpha-only dissolve |
| `Morph` | Morph posizionale dei glifi da layout A a layout B | Glyph positional morph |

Non c'è conflitto: servono scopi visivi diversi.

## Test esistenti

- `tests/text/test_animated_text_document.cpp` — certifica `Cut` come `Hold` fino al boundary e il mix di `DissolveLayouts`.
- `tests/text_golden/text_dissolve/text_dissolve.cpp` — golden frames al 0%, 25%, 50%, 75%, 100% della dissolve.

## Commit di chiusura
Documentazione aggiunta in `include/chronon3d/text/animated_text_document.hpp`.
