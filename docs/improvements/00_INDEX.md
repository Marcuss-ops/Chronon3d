================================================================================
  CHRONON3D — SUGGESTED IMPROVEMENTS
  Roadmap from current state to professional-grade engine
================================================================================

  Current state:  71 tests passing, shared_ptr→OwnedFB refactor complete,
                  incremental graph cache + skip-when-opaque implemented,
                  native video encoder with HWY SIMD YUV conversion (−92%).

  Target:         A tile-first, multi-threaded, GPU-capable render engine
                  with compiled display lists, persistent tile surfaces,
                  lock-free pipelines, and professional-quality output.

================================================================================
  TABLE OF CONTENTS
================================================================================

  TIER 1 — GAME CHANGERS (10-100× impact)
    1.  Parallel Tile Rendering (V3 Architecture)
    2.  Frame Graph Compiler (Compiled Display List)
    3.  Tile Surface Cache (Zero-Alloc Runtime)

  TIER 2 — QUICK WINS PROFESSIONALI (2-10× impact)
    4.  Box Blur 3-Pass Parallelization
    5.  Motion Blur Accumulation Parallel + SIMD
    6.  SPSC Lock-Free Queue for Encoder Pipeline
    7.  L1/L2 Prefetch in Hot Loops
    8.  Thread-to-Core Affinity + NUMA
    9.  any_cast Chain → Enum Dispatch
    10. RAII Guard for Thread-Local Ptrs
    11. Temporal Hashing — Cache Node with History
    12. ImageCache Sharding + Read-Write Lock
    13. Downsample SSAA Parallel + Raw Pointer Access
    14. Shadow/Glow Multi-Layer Fused Draw
    15. Persistent Disk Cache per Tile
    16. Scene Fingerprint Hash → XXH3
    17. Encoder Zero-Copy Frame Delivery
    18. Pool Adaptive Preallocation from Telemetry
    19. Transform Cache — Baked Animation Results

  TIER 3 — LONG-TERM VISION
    20. GPU Backend (Vulkan Compute)
    21. ECS Architecture (EnTT)
    22. Frame Graph with Resource Barriers (RDG-style)
    23. Persistent Daemon Mode
    24. Distributed Render Farm
    25. HarfBuzz Text Shaping Integration
    26. MSDF Font Atlas
    27. CI Multi-Platform (Windows + macOS)
    28. Layout Solver Extended
    29. Test Coverage for Missing Render Graph Nodes

