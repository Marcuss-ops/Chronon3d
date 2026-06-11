# Pipeline Status

## Modern rendering path (graph pipeline)

```
Composition
  → SoftwareRenderer::render_frame       (apps/chronon3d_cli/…/render_job_*.cpp)
    → render_composition_frame            (src/render_graph/pipeline/composition.cpp)
      → render_scene_via_graph            (src/render_graph/pipeline/scene.cpp)
        → [Phase 1] scene_frame_reuse     — early-out fast-paths
        → [Phase 2] scene_dirty_phase     — resolve + dirty rect
        → [Phase 3] scene_graph_phase     — graph build/reuse + pool prealloc
        → [Phase 4] scene_execute_phase   — tile/single-pass execution
        → [Phase 5] scene_commit_phase    — save state for next frame
        → GraphExecutor::execute          (src/render_graph/executor/)
```

**Entry points (CLI):**
- `chronon3d_cli render` — via `execute_render_job()` → `render_frame()`
- `chronon3d_cli video` — via video exporters → `render_frame()`
- `chronon3d_cli bench` — via `command_bench()` → `render_frame()`

**Key characteristics:**
- Render graph compilation + optimisation
- Dirty-rect / tile-based incremental rendering
- Cache: node cache, framebuffer pool, compiled graph cache
- Double-buffered render/write pipeline (WritePool)
- Per-frame telemetry collection
- SSAA / motion blur handled in `render_composition_frame()`


---

## Component ownership

| Component | Location | Status |
|-----------|----------|--------|
| `SoftwareRenderer::render_frame` | `src/backends/software/` | **Active** — modern entry point |
| `render_composition_frame` | `src/render_graph/pipeline/composition.cpp` | **Active** — composition-level orchestration |
| `render_scene_via_graph` | `src/render_graph/pipeline/scene.cpp` | **Active** — scene-level orchestration |
| 5 scene phases | `src/render_graph/pipeline/scene_*.cpp` | **Active** — phase decomposition |
| `GraphExecutor` | `src/render_graph/executor/` | **Active** — execution engine |
| `BenchRunner` | `include/chronon3d/runtime/bench_runner.hpp` | **Active** |
