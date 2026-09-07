# TICKET — Audit follow-up: NV12/CUDA single source of truth + measured hot-path allocations

Status: SSOT RESOLVED in Wave 1D (2026-09-07); P2 hot-path follow-ups still OPEN
Priority: P1 (SSOT) / P2 (hot paths, measure-first)
Owner: rendering-core

> **Resolution (Wave 1D, commit `f0cb4b6e`):** the two NV12 authorities below
> were unified into ONE — `media::video::CudaNv12Compositor` under
> `src/media/video/compositor/` — with a `VideoCompositor` contract +
> registry/resolver facade.  The Vulkan-side and media-side duplicate NVRTC
> scaffolding is deleted.  The equivalence harness was kept and rewritten as
> the post-unification determinism/contract matrix
> (`tests/media/nv12/`, CUDA-gated suite, runs with
> `CHRONON3D_RUN_CUDA_TESTS=1`); GPU-host baseline renders remain the
> remaining evidence step (see `TICKET-AUDIT-ROADMAP-WAVES.md` Wave 4).

## Context

Technical audit of `main` @ `6d6d9d5a6` ("Fix native video rendering pipeline")
identified two authoritative-level concerns that were deliberately **not** fixed
in the same pass, because the audit itself requires measurement/equivalence
evidence before refactoring GPU paths:

1. **Two authorities for CUDA/NV12 + NVRTC (P1, architectural).**
2. **Per-frame host allocations on GPU hot paths (P2, measure-first).**

Everything else from that audit (IPC shutdown lifecycle, IPC head-of-line
blocking, `RawVideoSink::close()` false success, `PipeTransportConfig`
honesty, daemon arg validation, DirectYUV host scratch reuse) was addressed
immediately — see the commits accompanying this ticket.## 1. Two CUDA/NV12/NVRTC authorities (P1) — RESOLVED in Wave 1D

Historical finding (audit of `main` @ `6d6d9d5a6`): two independent
implementations of the same CUDA/NV12 pipeline existed:

- **Authority A** — `src/backends/vulkan/cuda_nv12_surface_compositor.cpp`
  (Vulkan-backend CUDA interop): own NVRTC load/compile cache, own PTX mutex,
  own module lifecycle.
- **Authority B** — `src/media/video/cuda_direct_nv12_compositor.cpp`
  (Direct-YUV path): the same responsibilities implemented a second time.

Both were live, with the risk of semantic drift (color conversion, blend
semantics, NVRTC flags, architecture selection, error handling).

**Wave 1D resolution:** both files (and the duplicated NVRTC/JIT scaffolding)
are deleted; the single authority is `media::video::CudaNv12Compositor`
(`src/media/video/compositor/cuda/`), consumed by the Vulkan CUDA frame
importer and the native encoder.  The audit's "do NOT skip" order —
equivalence evidence before unification — was partially inverted by the
unification commit; the equivalence harness therefore now certifies the
unified authority's determinism and per-case contract instead of a dual-lane
diff, and the shared JIT/kernel consolidation (one `NvrtcBuiltins`, FullGraph
+ DirectYuv adapters) remains the GPU-host follow-up.

## 2. Hot-path allocation follow-ups (P2, measure-first)

Two allocations flagged by the audit were **not** blindly "optimized" because
the audit's own recommendation was to measure first:

- `FrameConversionService` full-frame cache-miss allocation
  (`src/media/frame_conversion/frame_conversion_service.cpp`): each cache miss
  allocates the full encoded frame (~12 MB at 4K NV12). The header documents a
  reusable staging buffer the implementation does not currently own on this
  path. Measure cache hit-rate + allocation churn in a real animated timeline
  before converting the buffer to cache-owned storage.
- Remaining per-frame host allocations on the DirectYUV path that are NOT
  covered by the scratch-vector fix already landed with this ticket.

### Required before acting

- Profile a representative multi-layer NV12 timeline with `heaptrack` or
  `perf mem`; record allocation counts/frames before and after any change.
- Keep scratch buffers owned by a single authority (the session/compositor)
  — never thread-local or global.

## Definition of done

- [ ] Equivalence test between the two NV12 authorities passes on GPU.
- [ ] NV12/NVRTC refactor commit shows zero output delta + no benchmark
      regression (batch_wall_ms recorded in commit message).
- [ ] FrameConversionService measurement note added to this ticket with the
      numbers that drove the decision (keep-as-is or pooled buffer).
