# TICKET-P1-11 — Timeline e composizione: troppi percorsi concorrenti

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | timeline / compositor |
| **Stato** | PLANNED |
| **Blocca** | post-baseline |
| **Feature Freeze** | ❌ Bloccato — richiede baseline verde |

## Bug

Coesistono 6+ percorsi concorrenti: `Composition::evaluate()`, `compile_composition() + evaluate()`, `graph::render_composition_frame()`, `runtime::RenderPipeline::render_composition()`, `RenderEngine::render()`, `sdk::RenderEngine::render()`. `compile_composition()` usa `shared_ptr` non proprietario con deleter vuoto — dangling pointer se il definition originale muore. Fingerprint calcolato su memoria grezza di tipi non banali (padding, puntatori, layout STL).

## Criteri di accettazione

- [ ] Unificare in unico percorso canonico: `CompositionDefinition → CompositionCompiler → CompiledComposition → CompositionEvaluator → EvaluatedFrame → FrameGraphCompiler → GraphExecutor → RenderOutput`
- [ ] `CompiledComposition` possiede OWN copy della definizione (no shared_ptr non proprietario)
- [ ] Sostituire fingerprint su memoria grezza con hash deterministico per campo
- [ ] Deprecare i percorsi concorrenti
- [ ] Test: compiled composition sopravvive alla distruzione del definition originale
- [ ] Test: fingerprint identico su due macchine diverse per la stessa composizione

## File interessati

- `src/animation_compositions.cpp`, `content/animation_compositions.cpp`
- `include/chronon3d/animation_compositions.hpp`
- `src/render_graph/`, `src/runtime/`
