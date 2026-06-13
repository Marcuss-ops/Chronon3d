================================================================================
  TIER 3 — LONG-TERM VISION
================================================================================

  20. GPU BACKEND (Vulkan Compute)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    All effects (blur, glow, color grading) are CPU-bound — 10-20× slower than
    GPU alternatives. Compute shaders on modern GPUs can process pixels at
    10-100× the throughput of CPU SIMD.

  Solution:
    Create a Vulkan compute backend that handles EffectStack and compositing.
    Zero-copy with shared memory (VK_KHR_external_memory). The rest of the
    pipeline remains on CPU.

  New files:
    - src/backends/vulkan/vulkan_compute_engine.cpp
    - src/backends/vulkan/vulkan_compute_engine.hpp
    - src/backends/vulkan/shaders/gaussian_blur.comp
    - src/backends/vulkan/shaders/composite_normal.comp
    - src/backends/vulkan/shaders/glow.comp
    - CMakeLists.txt target

  Key steps:
    1. Initialize VkInstance + VkDevice with VK_KHR_external_memory
    2. Allocate VkBuffer shared between CPU and GPU
    3. Compile compute shaders at build time via glslc
    4. Bind buffer → dispatch → readback
    5. Fallback to CPU if GPU unavailable

  Expected impact: Blur/gaussian → 10-20× faster. Glow → 15×. This is the
                    single biggest architectural change.

  Complexity: Very High (1-2 months)

  ──────────────────────────────────────────────────────────────────────────────

  21. ECS ARCHITECTURE (EnTT)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The layer system is object-oriented (AoS layout) → cache misses when
    iterating all layers. The Scene evaluates layers one by one with virtual
    function calls.

  Solution:
    Rewrite the layer system using ECS (EnTT library, already a dependency).
    Entities = IDs, Components = pure data arrays (SoA), Systems = batch
    functions.

  New files:
    - include/chronon3d/ecs/components.hpp
    - include/chronon3d/ecs/systems.hpp
    - src/ecs/animation_system.cpp
    - src/ecs/culling_system.cpp
    - src/ecs/render_node_system.cpp
    - src/ecs/transform_system.cpp

  Key steps:
    1. Define components: Position, Transform, Opacity, EffectStack, Shape
    2. Implement systems: AnimationSystem, CullingSystem, RenderNodeSystem
    3. Rewrite Scene::evaluate() to use ECS instead of OOP loops
    4. Profile: verify cache-friendly iteration

  Expected impact: Layer update pipeline → 2-3× faster, less RAM bandwidth.
                    Performance-portable to SIMD and GPU.

  Complexity: Very High (1-2 months)

  ──────────────────────────────────────────────────────────────────────────────

  22. FRAME GRAPH WITH RESOURCE BARRIERS (RDG-style)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    There's no formal read/write dependency analysis between passes → redundant
    memcpy and allocations. The executor doesn't know which framebuffers alias.

  Solution:
    Each pass declares explicit read/write buffers. The builder automatically
    inserts aliasing + prune. Pattern from Frostbite Engine / UE5 Render Graph.

  New files:
    - include/chronon3d/render_graph/rdg.hpp
    - src/render_graph/rdg_builder.cpp

  Key steps:
    1. Define RenderPass with {name, inputs[], outputs[], barrier_type}
    2. Implement TransientResourceAnalysis — max memory footprint per frame
    3. Integrate with existing graph builder → each node becomes a pass
    4. Automatic buffer aliasing when lifetimes don't overlap

  Expected impact: -30% memcpy, pool preallocated statically per frame.
                    Enables GPU backend by providing clear resource barriers.

  Complexity: High (3-4 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  23. PERSISTENT DAEMON MODE
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Each run pays startup time (init, warmup, alloc). For CLI usage with many
    short clips, this adds up.

  Solution:
    Keep Chronon3D as a daemon listening on a Unix socket. Zero startup time
    for subsequent renders. The renderer stays warm, the pool stays hot.

  New files:
    - apps/chronon3d_daemon/main.cpp
    - include/chronon3d/daemon/daemon_protocol.hpp
    - src/daemon/daemon_server.cpp

  Key steps:
    1. Create daemon.cpp with main loop accepting socket connections
    2. Define JSON protocol (or FlatBuffers for zero-parse)
    3. Daemon keeps renderer warm and pool hot permanently
    4. chronon3d_cli render becomes a lightweight client

  Expected impact: Zero startup time for subsequent renders. Warm cache always.
                    Enables render farm by providing a network protocol.

  Complexity: Medium (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  24. DISTRIBUTED RENDER FARM
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    A single machine is limited to its core count. For large batch renders
    (YouTube channels, studios), throughput is bounded by one box.

  Solution:
    Split the timeline into segments, distribute to N machines, merge final
    output. Linear scalability — 4 machines = 4× throughput.

  New files:
    - include/chronon3d/distributed/timeline_partitioner.hpp
    - include/chronon3d/distributed/render_slave.hpp
    - include/chronon3d/distributed/render_master.hpp
    - src/distributed/render_slave.cpp
    - src/distributed/render_master.cpp
    - proto/chronon3d_render.proto (gRPC)

  Key steps:
    1. Implement TimelinePartitioner — splits into N equal segments
    2. Create render_slave that receives a range and renders via daemon protocol
    3. RPC layer (gRPC or HTTP REST) to orchestrate slaves
    4. render_master coordinates and does final ffmpeg concat merge

  Expected impact: Linear scalability — 4 machines = 4× throughput.
                    Enables studio-level batch rendering.

  Complexity: Very High (1-2 months)

  ──────────────────────────────────────────────────────────────────────────────

  25. HARFBUZZ TEXT SHAPING INTEGRATION
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Text rendering uses a custom layout engine. Limited support for non-Latin
    languages, ligatures, and complex glyph forms.

  Solution:
    Integrate HarfBuzz for glyph shaping — the industry standard used by Chrome,
    Firefox, Android, and LibreOffice.

  New dependencies:
    - harfbuzz (vcpkg)

  Files to modify:
    - src/backends/text/text_layout_engine.cpp
    - include/chronon3d/backends/text/text_layout_engine.hpp

  Key steps:
    1. Add HarfBuzz as vcpkg dependency
    2. Create hb_shape(text, font) → glyph positions
    3. Integrate with existing TextLayoutEngine
    4. Benchmark: verify shaping doesn't slow text rendering

  Expected impact: Full Unicode support, correct shaping for Arabic, Hindi,
                    Thai, etc. Professional-quality text rendering.

  Complexity: High (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  26. MSDF FONT ATLAS FOR TEXT SCALABILITY
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Text scaling/rotation on rasterized textures causes blur or pixelation.
    Each size requires a separate rasterization.

  Solution:
    Multi-channel Signed Distance Fields (MSDF) — glyph stored as distance to
    edges, not as pixel bitmap. Renders perfectly at any scale.

  New files:
    - src/backends/text/msdf_generator.cpp
    - include/chronon3d/backends/text/msdf_atlas.hpp
    - shaders/msdf_reconstruction.glsl (for GPU)

  Key steps:
    1. Integrate chlumsky/msdfgen (header-only, MIT license)
    2. Generate MSDF atlas for each font in use
    3. Modify rasterize_text_to_bl_image() — use MSDF when available
    4. Reconstruction shader: distance → alpha with smoothing

  Expected impact: Perfect text at any resolution. Instant scaling without
                    re-rasterize. GPU-friendly text rendering.

  Complexity: High (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  27. CI MULTI-PLATFORM (Windows + macOS)
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    CI currently builds and tests only on Linux. Platform-specific bugs
    (Windows path encoding, macOS mmap, incorrect ifdefs) go undetected.

  Solution:
    Add matrix build with ubuntu-latest, windows-latest, macos-latest to the
    existing CI workflow.

  Files to modify:
    - .github/workflows/ci.yml

  Key steps:
    1. Add strategy: matrix: { os: [ubuntu-latest, windows-latest, macos-latest] }
    2. Adapt build steps for each OS (vcpkg setup, CMake presets)
    3. Filter Linux-only tests (e.g. io_uring)
    4. Verify all tests pass on all platforms

  Expected impact: Zero platform bugs in production. Real multi-OS coverage.
                    1-2 days.

  Complexity: Medium

  ──────────────────────────────────────────────────────────────────────────────

  28. LAYOUT SOLVER EXTENDED
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    The layout solver supports only pin-to-anchor and safe area. No flexbox,
    flow, or auto-layout for multi-line text and grids.

  Solution:
    Implement layout flow for text compositions and simple grid layouts.

  Files to modify:
    - src/layout/layout_solver.cpp
    - include/chronon3d/layout/layout_rules.hpp

  Key steps:
    1. Add LayoutFlow — row-based arrangement with wrap
    2. Add LayoutGrid — uniform cell grid
    3. Integrate with LayoutSolver::solve()
    4. Add unit tests for new layout modes

  Expected impact: Feature parity with professional compositing tools. Enables
                    complex multi-layer auto-layout.

  Complexity: Medium (2-3 weeks)

  ──────────────────────────────────────────────────────────────────────────────

  29. TEST COVERAGE FOR MISSING RENDER GRAPH NODES
  ──────────────────────────────────────────────────────────────────────────────

  Problem:
    Only a subset of RenderNode types have dedicated tests. Complex nodes like
    ShadowNode, GlowNode, DoFNode, MaskNode have no unit tests. Bugs in these
    nodes are silent until visual render.

  Files to create:
    - tests/render_graph/test_shadow_node.cpp
    - tests/render_graph/test_glow_node.cpp
    - tests/render_graph/test_dof_node.cpp
    - tests/render_graph/test_mask_node.cpp
    - tests/render_graph/test_bloom_node.cpp

  Key steps:
    1. Identify all nodes without tests (compare nodes/*.hpp with tests/*.cpp)
    2. Write tests for each: synthetic inputs, output verified via hash/pixel
    3. Use existing pattern: RenderFixtures + pixel_assertions + golden refs
    4. Register in test_registry and run in CI

  Expected impact: Automatic regression detection. Safe refactoring. Fewer
                    visual bugs. 3-5 days.

  Complexity: Medium


