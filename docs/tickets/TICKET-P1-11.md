# TICKET-P1-11 — Timeline e composizione: troppi percorsi concorrenti

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | timeline / compositor |
| **Stato** | DONE (commit `8b0c85d7`) |
| **Blocca** | — |
| **Feature Freeze** | ✅ Completato — modifiche consentite (correzione build/deprecazione) |

## Bug

Coesistono 6+ percorsi concorrenti: `Composition::evaluate()`, `compile_composition() + evaluate()`, `graph::render_composition_frame()`, `runtime::RenderPipeline::render_composition()`, `RenderEngine::render()`, `sdk::RenderEngine::render()`. `compile_composition()` usa `shared_ptr` non proprietario con deleter vuoto — dangling pointer se il definition originale muore. Fingerprint calcolato su memoria grezza di tipi non banali (padding, puntatori, layout STL).

## Criteri di accettazione

- [x] Unificare in unico percorso canonico: `CompositionDefinition → CompositionCompiler → CompiledComposition → CompositionEvaluator → EvaluatedFrame → FrameGraphCompiler → GraphExecutor → RenderOutput`
- [x] `CompiledComposition` possiede OWN copy della definizione (no shared_ptr non proprietario)
- [x] Sostituire fingerprint su memoria grezza con hash deterministico per campo
- [x] Deprecare i percorsi concorrenti
- [x] Test: compiled composition sopravvive alla distruzione del definition originale
- [x] Test: fingerprint identico su due macchine diverse per la stessa composizione

## Nota tecnica — CompositionCompileContext

`CompositionCompileContext` resta parte delle firme pubbliche di
`compile_camera()` e `compile_composition()` come options carrier esplicito.
Il precedente campo `compiled_at` è stato rimosso perché non aveva consumer:
non influenzava compilazione, diagnostica, fingerprint o ciclo detection e
introduceva soltanto una dipendenza dall'orologio di sistema. Il contesto è
intenzionalmente vuoto finché non esisteranno opzioni reali; nuove opzioni di
compilazione dovranno essere aggiunte qui, senza moltiplicare gli argomenti
pubblici né reintrodurre input non deterministici.

La copertura deterministica verifica che contesti value-initialized distinti
producano lo stesso fingerprint per la stessa `CompositionDefinition`.

## File interessati

- `src/animation_compositions.cpp`, `content/animation_compositions.cpp`  <!-- drift-allow: stale-ref -->
- `include/chronon3d/animation_compositions.hpp`  <!-- drift-allow: stale-ref -->
- `src/render_graph/`, `src/runtime/`
