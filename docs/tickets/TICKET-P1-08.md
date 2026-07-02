# TICKET-P1-08 — Text renderer hotspot monolitico

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | text / backends |
| **Stato** | PLANNED |
| **Blocca** | post-baseline |
| **Feature Freeze** | ❌ Bloccato — richiede baseline verde |

## Bug

`text_run_processor.cpp` supera le 900 linee e gestisce nello stesso TU: font resolution, scratch pooling, bbox, supersampling, blur, shadow, stroke, fill, crossfade, compositing, diagnostics. `draw_text_run()` è orchestratore a otto stage con una grande lambda catturante.

Per ogni chiamata vengono ricreati vettori di span handles, font, glyph-span mapping, 5 vector per blur tier, superfici supersampled. `TextRunShape` conserva `FontEngine*` raw. `shared_text_layout_cache()` è ancora presente.

## Criteri di accettazione

- [ ] Progettare `CompiledTextRun` con tutti i dati pre-risolti (layout, font, glyph-to-span, blur plan)
- [ ] Estrarre la lambda di draw in una funzione standalone con context tipizzato
- [ ] Spostare font resolution + BLFont creation fuori dal draw path
- [ ] Pre-compilare blur plan e glyph-to-span mapping
- [ ] Sostituire `FontEngine*` raw in `TextRunShape` con riferimento posseduto
- [ ] Eliminare `shared_text_layout_cache()` dopo migrazione callsite
- [ ] Test: draw_text_run con CompiledTextRun pre-risolto produce output identico

## File interessati

- `src/backends/software/processors/text_run/text_run_processor.cpp`
- `include/chronon3d/text/text_run_shape.hpp`
- `src/text/text_run.cpp`
