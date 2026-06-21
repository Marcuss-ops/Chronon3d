# SoftwareRenderer — Audit (R1)

Generato da `tools/audit_software_renderer.sh` su commit `2ceecc1d`.

## Metriche di base

| Metrica | Valore | Target R4 / R3 |
|---|---:|---|
| Header LOC (`software_renderer.hpp`) | 467 | ≤ 200 |
| \#include \<chronon3d/\> non-locali nell'header | 28 | ≤ 6 |
| \#include "..." locali nell'header | 0
0 | n/a |
| Blocchi `#ifdef`/`#if defined` nel cpp | 8 | n/a |
| Metodi pubblici totali | 13 | n/a |
| Override `override` (candidati R3a) | 12 | 0 al termine di R3 |
| Inoltri puri `m_runtime->backend().` (R3a) | 4 | 0 al termine di R3 |
| Inoltri `m_runtime->` totali (forwarders) | 10 | n/a |

## Doppia identità renderer/backend

```cpp
107:class SoftwareRenderer : public Renderer, public graph::RenderBackend {
```

## dynamic_cast / static_cast verso SoftwareRenderer

Trovati `dynamic_cast<SoftwareDesigner*>` = **7**, `static_cast<SoftwareRenderer*>` = **0
0**.

Dettaglio `dynamic_cast`:

```text
  src/render_graph/pipeline/composition.cpp:53:    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);
  src/render_graph/pipeline/scene.cpp:106:    SoftwareRenderer* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend);
  src/render_graph/builder/passes/graph_builder_layer_pipeline_pass.cpp:26:        auto* renderer = dynamic_cast<SoftwareRenderer*>(rctx.services.backend);
  src/render_graph/nodes/clear_node.cpp:24:    auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(ctx.services.backend);
  src/render_graph/preflight/preflight.cpp:58:    if (auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(&backend)) {
  apps/chronon3d_cli/utils/video/video_job_dry_run.cpp:33:        auto* sw_renderer = dynamic_cast<SoftwareRenderer*>(renderer.get());
  apps/chronon3d_cli/commands/video/exporters/pipe_export_session_setup.cpp:83:    session->sw_renderer = dynamic_cast<SoftwareRenderer*>(session->renderer.get());
```

## SoftwareRenderer& nelle superfici dei processor / runtime

Totale grezzo in processori e render_graph: **34**.

Dettaglio (prime 40 occorrenze):

```text
  src/backends/software/software_backend.cpp:66:    // ShapeProcessor::draw() currently takes SoftwareRenderer& (not RenderBackend&).
  src/backends/software/software_renderer.cpp:70:// of SoftwareRenderer&).
  src/backends/software/software_renderer.cpp:218:        const_cast<SoftwareRenderer&>(*this),
  src/backends/software/processors/path_processor.cpp:16:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
  src/backends/software/processors/software_utility_processors.cpp:12:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/software_utility_processors.cpp:39:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/software_grid_background_processor.cpp:12:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/software_mesh_processor.cpp:9:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/text/text_glow.cpp:163:void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
  src/backends/software/processors/text/software_text_processor.cpp:42:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/text/software_text_processor.cpp:200:            // `SoftwareRenderer&` argument (signature is fixed), so we
  src/backends/software/processors/text/text_shadow.cpp:17:void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
  src/backends/software/processors/text/software_text_effects.hpp:9:    SoftwareRenderer& renderer,
  src/backends/software/processors/text/software_text_effects.hpp:18:    SoftwareRenderer& renderer,
  src/backends/software/processors/text/text_effects.hpp:16:void draw_text_glow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
  src/backends/software/processors/text/text_effects.hpp:20:void draw_text_shadow(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
  src/backends/software/processors/software_line_processor.cpp:9:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/image/software_image_processor.cpp:12:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/image/software_tiled_image_processor.cpp:11:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/backends/software/processors/text_run/text_run_processor.cpp:243:    SoftwareRenderer& renderer,
  src/backends/software/processors/text_run/text_run_processor.cpp:810:        void draw(SoftwareRenderer&, Framebuffer&, const RenderNode&,
  src/backends/software/processors/software_shape_processor.cpp:11:    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state,
  src/runtime/render_engine.cpp:197:SoftwareRenderer&       RenderEngine::renderer() noexcept       { return *m_impl->m_renderer; }
  src/runtime/render_engine.cpp:198:const SoftwareRenderer& RenderEngine::renderer() const noexcept { return *m_impl->m_renderer; }
  src/runtime/renderer_warmup.cpp:8:    SoftwareRenderer& renderer,
  src/runtime/render_pipeline.cpp:25:RenderPipeline::RenderPipeline(SoftwareRenderer& renderer, RenderRuntime& runtime) noexcept
  include/chronon3d/backends/software/text_run_processor.hpp:52:    SoftwareRenderer& renderer,
  include/chronon3d/backends/software/software_renderer.hpp:136:    SoftwareRenderer(const SoftwareRenderer&) = delete;
  include/chronon3d/backends/software/software_renderer.hpp:137:    SoftwareRenderer& operator=(const SoftwareRenderer&) = delete;
  include/chronon3d/backends/software/software_renderer.hpp:138:    SoftwareRenderer(SoftwareRenderer&&) noexcept = default;
  include/chronon3d/backends/software/software_renderer.hpp:139:    SoftwareRenderer& operator=(SoftwareRenderer&&) noexcept = default;
  include/chronon3d/backends/software/software_backend.hpp:13:// SoftwareRenderer& (draw_node, draw_text_run) remain on SoftwareRenderer
  include/chronon3d/backends/software/software_backend.hpp:79:    // ShapeProcessor::draw() takes SoftwareRenderer& (not RenderBackend&).
  include/chronon3d/backends/software/shape_processor.hpp:35:    virtual void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node, const RenderState& state, 
```

## Metodi pubblici dell'header (euristica)

```text
  SoftwareRenderer(SoftwareRenderer&&)
  SoftwareRenderer(const
  commit_prev_frame_state(frame,
  explicit
  graph::RenderOpResult
  node_cache().clear();
  renderer::clear_text_glow_cache();
  renderer::clear_text_shadow_cache();
  std::shared_ptr<Framebuffer>
  std::shared_ptr<cache::FramebufferPool>
  structure_fp,
  void
  ~SoftwareRenderer()
```

## Classificazione per categoria

Da compilare in PR R1.1 — colonne previste: backend / orchestrazione /
settings / telemetry / cache / servizio.
