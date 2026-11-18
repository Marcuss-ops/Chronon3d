# Inline Mesh Static Buffers & Compute Tile Binning Benchmark (2026-11-17)

- **Device:** NVIDIA RTX A4000 (`timestampPeriod = 1.00 ns`)
- **Canvas / BBox:** `512×512` (`32×32` tiles of `16×16` pixels)
- **Ticket:** `docs/tickets/TICKET-PERF-INLINE-MESH-STATIC-BUFFERS-AND-BINNING-20261117.md`

## GPU Time Comparison (Before vs After)

| Triangles | Unbinned (`inline_mesh.comp` brute-force) | Tile-Binned (`inline_mesh_bin.comp` + `inline_mesh.comp`) | Speedup |
| ---: | ---: | ---: | ---: |
| 10 | 0.0375 ms | 0.0259 ms | 1.45× |
| 100 | 0.3095 ms | 0.0258 ms | 12.00× |
| 1,000 | 3.0012 ms | 0.0432 ms | 69.47× |
| 10,000 | 32.3706 ms | 0.3278 ms | **98.75×** |

## Summary of Changes

1. **Static Index Buffer Separation (`vulkan_backend_inline_mesh_private.cpp`)**:
   - Indices (`std::span<const std::uint32_t>`) are cached by 64-bit FNV-1a hash + byte size in a 16-slot LRU (`VulkanBackend::Impl::inline_mesh_static_indices`) inside the Vulkan backend (no new cache authority; `cache_family_inventory` unchanged).
   - Steady-state per-frame `draw_inline_mesh` uploads only `projected_lit_vertices` (zero `std::memcpy` of `indices`).
2. **Compute Tile Binning (`inline_mesh_bin.comp` + `inline_mesh.comp` specialization constants)**:
   - `inline_mesh.comp` adds `kSpecUseTileBins` and `kSpecSeparateIndexBuffer` (`GpuKernelId::InlineMesh` and `GpuKernelId::InlineMeshBinned`), avoiding shader source duplication.
   - `inline_mesh_bin.comp` (`GpuKernelId::InlineMeshBin`, `local_size_x = 64`) bins triangles into `16×16` screen tiles (`kMaxTrisPerTile = 256`, with automatic full-scan fallback on tile overflow) for meshes with `tri_count > 64`.
