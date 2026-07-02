# TICKET-P1-10 — Frame rate hardcoded nella pipeline

| Campo | Valore |
|-------|--------|
| **Priorità** | P1 |
| **Area** | render / runtime |
| **Stato** | ✅ DONE (`6df9b429`, `560750e3`) |
| **Blocca** | — |
| **Feature Freeze** | ✅ Completato |

## Bug (risolto)

`RenderPipeline::render_scene()` e `debug_graph()` passavano sempre `30.0f`. L'API graph aveva `float fps = 30.0f` come default. Questo contaminava animazioni sub-frame, motion blur, video decoding, expression time, temporal cache keys, simulazioni stateful.

## Azioni completate

- [x] Rimuovere default `fps = 30.0f` da `render_scene_via_graph()`, `debug_scene_graph()`, `analyze_scene_graph()`
- [x] Aggiungere `float fps` obbligatorio a `RenderPipeline::render_scene()`, `SoftwareRenderer::render_scene()`, `RenderEngine::render_scene()`, base `Renderer`
- [x] Aggiungere `sdk::FrameRate` a `sdk::RenderRequest`
- [x] Aggiornare CLI (`command_graph.cpp`, `command_bench.cpp`) e tutti i test caller con `30.0f` esplicito
- [x] Fix `analyze_scene_graph` parameter order (fps prima di execute/include_dot defaults)

## File modificati

17 file: `render_pipeline.hpp` (graph + runtime), `render_request.hpp`, `renderer.hpp`, `software_renderer.hpp/cpp`, `render_engine.hpp/cpp`, `render_pipeline.cpp`, `command_graph.cpp`, `command_bench.cpp`, `renderer_warmup.cpp`, test files
