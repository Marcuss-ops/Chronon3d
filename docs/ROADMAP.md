# Chronon3D — Roadmap Attiva

> Ogni voce ha una delle tre categorie:
> - 🟡 **Partial** — parzialmente implementato; mancano pezzi significativi.
> - 🔵 **Planned** — non ancora implementato.
>
> Gli item completati (✅ Verified) sono in [`CHANGELOG.md`](CHANGELOG.md).
> Ultimo aggiornamento: 2026-06-17

---

## 🟡 Partial — Parzialmente implementato

### N1. Motion Blur Accumulation + SIMD
**Mancante:** SIMD-izzazione accumulazione con Highway non verificata.
**Dove:** `include/chronon3d/render_graph/nodes/velocity_buffer_motion_blur.hpp`, `src/render_graph/pipeline/composition.cpp`
**Test:** `tests/render_graph/test_velocity_buffer_motion_blur.cpp`, `tests/render_graph/test_post_processing_system.cpp`

### N2. Box Blur 3-Pass Parallelizzato
**Mancante:** Verifica che 3-pass box blur (horizontal → vertical → separato) sia effettivamente implementato.
**Dove:** `src/backends/software/utils/effects/effect_blur.cpp`

### N6. Blend mode switch → template specialization
**Stato:** Highway SIMD kernels per blending ma switch-case nel dispatcher.
**Dove:** `src/backends/software/simd/highway_color_kernels.cpp`
**Benchmark:** `tests/bench/micro_benchmarks.cpp`

### N8. Temp FB aliasing shared_ptr
**Stato:** `state.shared_transparent` condiviso tra nodi che non modificano il FB. Pattern parziale.
**Dove:** `src/render_graph/executor/node_runner.cpp`

### N13. Layout Solver Minimalista
**Mancante:** `LayoutFlow` (wrap) e `LayoutGrid` (griglia uniforme) non implementati.
**Dove:** `include/chronon3d/layout/layout_rules.hpp`, `src/scene/layout_solver.cpp`
**Test:** `tests/layout/test_design_kit.cpp`

### N16. Zero-Copy Frame Delivery all'Encoder
**Mancante:** Implementazione zero-copy AVFrame creation.
**Dove:** `apps/chronon3d_cli/commands/video/exporters/`

### N17. Pool Miss Reason Dashboard
**Mancante:** Dashboard UI per visualizzare distribuzione miss reason.
**Dove:** `src/runtime/telemetry/sqlite/sqlite_telemetry_store.cpp`
**Frontend:** `tools/telemetry_dashboard/`

### M3. SPSC Lock-Free Queue per Pipe FFmpeg
**Stato:** Mutex + condition variable nel pipe encoder (async slot state machine).
**Mancante:** Circular buffer SPSC lock-free.
**Dove:** `apps/chronon3d_cli/commands/video/exporters/`

### M11. Test Coverage Nodi Render Graph Mancanti
**Mancante:** Test dedicati per ShadowNode, GlowNode, BloomNode, GradientNode, DoFNode, MaskNode.
**Test esistenti:** `tests/render_graph/pipeline/`, `tests/render_graph/builder/`, `tests/render_graph/compiler/`

---

## 🔵 Planned — Non ancora implementato

### N3. Downsample SSAA Parallel
**Soluzione:** Accesso row pointer diretto + TBB + SIMD box filter.
**Guadagno stimato:** 2-4× con SSAA 2×.

### N15. Framebuffer Pool Adaptive Preallocation
**Problema:** Contatori telemetry mostrano miss ma pool non si adatta.
**Soluzione:** `FramebufferPool::adapt_pool()` — se miss > 0 per 3 frame consecutivi, aumenta pool del 50%.
**Dove:** `src/cache/framebuffer_pool.cpp`

### M2. ISPC per il Blur Gaussiano
**Problema:** apply_blur è scalar o usa Highway (1-8 pixel per istruzione).
**Soluzione:** ISPC → 8 pixel per istruzione AVX2.
**Note:** Highway SIMD già presente come alternativa. Valutare se ISPC aggiunge vantaggio reale.

### M4. Render Speculativo — Multi-Frame Ahead
**Problema:** Solo frame N renderizzato → core idle.
**Soluzione:** Worker pool renderizza N+1..N+15 in anticipo.
**Guadagno stimato:** Fino a 16× throughput teorico.

### I4-EXT. NUMA-Aware FB Pool Allocation
**Problema:** FB pool allocates indipendentemente dalla topologia NUMA.
**Soluzione:** Allocare FB pool sul nodo NUMA locale al thread.
**Prerequisito:** I4 thread pinning è già attivo.

---

## 🌀 LONG-TERM — Prossimi mesi

| ID | Descrizione | Impatto | Stato |
|----|-------------|---------|-------|
| L1 | GPU Backend per EffectStack (Vulkan Compute) | 🔴 Alto | 🔵 Planned |
| L2 | ECS Architecture (Entity Component System) | 🟡 Medio | 🔵 Planned |
| L3 | Frame Graph con Resource Barriers (RDG-style) | 🔴 Alto | 🔵 Planned |
| L4 | Persistent Daemon Mode (Hot Server) | 🔴 Alto | 🔵 Planned |
| L7 | MSDF Font Atlas per Text Scalability | 🟡 Medio | 🔵 Planned |
| L8 | Parallel Tile Rendering (Bucket-Based) | 🔴 Alto | 🔵 Planned |
| L5 | Render Farm Distribuito (Multi-Host) | 🔴 Alto | 🔵 Planned |
| L9 | CI Multi-Platform (Windows + macOS) | 🟡 Medio | 🔵 Planned |

---

## 🔧 Quick Win (1 giorno o meno)

| ID | Descrizione | Dove | Stato |
|----|-------------|------|-------|
| N6 | Blend mode switch → template specialization | `blend_mode.hpp` | 🟡 Partial — Highway SIMD ma switch-case |
| N7 | Shadow/Glow multi-layer fused | `effect_stack.cpp` | 🔵 Planned |
| N8 | Temp FB aliasing shared_ptr | `effects_internal.hpp` | 🟡 Partial |
| N9 | Trace lock-free queue | `trace.hpp` / `trace.cpp` | 🔵 Planned |
| N10 | RAII guard thread_local ptrs | `software_renderer.cpp` | 🔵 Planned |
