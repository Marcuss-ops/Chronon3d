# DOD runtime CPU pass

Status: implemented and CPU/native verified on 2026-09-06.

## Completed

- Dirty root collection captures the backend processor-registry snapshot once
  and borrows processors from it; it no longer rebuilds a snapshot per root.
- Dirty layer workers use contiguous TLS entries and perform a single final
  materialization for the legacy name-keyed history boundary. This removes
  worker-local hash buckets and string allocations from bbox collection.
- PreparedRenderJob owns three parked stage workers for its lifetime. Batches
  publish slot ids through the existing bounded queues instead of creating and
  joining three threads for every `render_frames()` call.
- Frame-scoped executor error storage and node-memory tracking are non-owning
  on the successful path; stable job values such as `assets_root` are prepared
  once rather than reconstructed for every frame.
- Native FFmpeg telemetry separates prepare, copy/conversion, send/receive,
  mux, flush, trailer and close work. The native-format path bypasses
  `sws_scale`; encoded packets move synchronously to the muxer without
  `av_packet_clone` or a `shared_ptr` control block.

## Explicitly deferred

`Framebuffer` still crosses the render-to-encoder boundary as
`std::shared_ptr<Framebuffer>`. The public render APIs and cache reuse paths
also return that type, so changing the slot payload alone would create an
unsafe mixed lifetime protocol. A later, separately benchmarked migration
must change the renderer output contract to slot-owned/unique framebuffer
handles end-to-end, then remove the remaining `use_count()` reuse heuristic.

Likewise, the public dirty-history and FrameDelta compatibility surfaces remain
name-keyed. Replacing them with stable `LayerID` tables requires a compiled
scene identity authority and a migration for external/test callers; it is not
safe to assign positional ids ad hoc in the frame path. The current pass
removes the repeated worker-local map churn while preserving that contract.

Vulkan/CUDA synchronization and native GPU-frame handoff are intentionally
outside this CPU-only host pass.
