# TICKET — Text interop authority, bounded caches, and tile overflow

## Scope

The text execution path keeps the packed-atlas fallback for compatibility,
while the prepared MTSDF path is the preferred lowering. DirectYUV now consumes
the same canonical prepared layout and MTSDF generator, then decodes the
bit-preserving RGBA8 atlas directly in CUDA. Vulkan↔CUDA timeline
values are owned by one shared `InteropGeneration` per exported physical
surface.  Prepared text and packed-atlas residency use the canonical bounded
`cache::LruCache` primitive.

## Demolition Debt — packed atlas fallback

- **Owner:** Text/backend maintainers.
- **Reason:** the Vulkan compatibility caller still needs the CPU packed atlas
  while the prepared MTSDF route is being certified across all supported
  effects and backends.
- **Exit condition:** the prepared text program lowers equivalently to Vulkan
  RGBA and DirectYUV for the supported text corpus, with zero production
  callers of `GpuTextAtlasCache`, `PackedGlyphBitmap`, and the packed-atlas
  builder.
- **Equivalence test/gate:** byte-stable prepared-text corpus comparison for
  fill, stroke, shadow, reveal/typewriter animation, and multi-font spans.
- **Removal scope:** `GpuTextAtlasCache`, `PackedGlyphBitmap`, packed-atlas
  construction in `gpu_text_run.cpp`, and the compatibility counters/tests.
- **Status:** `ACTIVE`.

## Current implementation status

- Pointer identity is no longer a cache key; prepared runs use the immutable
  `TextRunLayout::layout_hash()` fingerprint.
- Both text caches are bounded and 16-way sharded for concurrent access;
  same-key atlas population is coalesced.
- Tile binning marks overflow atomically and the raster pass fails closed for
  an overfull tile instead of silently dropping contributor 257+.
- A recreated CUDA bridge retains the Vulkan-owned generation authority.
- DirectYUV no longer depends on Blend2D for text: it uploads the shared MTSDF
  atlas format and performs field/color lowering in the CUDA batch kernel.
- DirectYUV capability resolution now rejects multi-node overlays and
  multi-font layouts before prepare/execute, matching the certified Vulkan
  MTSDF contract; the text rasterizer no longer accepts ignored style/layout
  arguments.
- A failed drawable glyph is fail-closed in both MTSDF lowerings; the Vulkan
  compatibility fallback increments `gpu_text_mtsdf_fallback_count` so a
  successful packed fallback cannot hide a canonical-lowering failure.
- DirectYUV frame/template wrappers use bounded 16-entry reuse pools and the
  resource index is sorted for allocation-free binary lookup.
- `TextRunLayout::layout_hash()` includes fallback font spans, preventing
  distinct shaped authorities from aliasing a prepared run.
- DirectYUV MTSDF rasterization is now daemon-lifetime and coalesced through
  `DirectYuvTextTextureCache`, a 16-shard byte-weighted LRU keyed by the same
  immutable layout fingerprint. Repeated programs therefore reuse the CPU
  atlas preparation; per-device CUDA upload/residency remains owned by
  `VideoDeviceRuntime`.

GPU execution and same-SHA hardware certification remain `NOT RUN` on hosts
without Vulkan/CUDA hardware.
