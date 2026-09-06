# TICKET — Audit follow-up: NV12/CUDA single source of truth + measured hot-path allocations

Status: Open (deferred by design — see the audit note below)
Priority: P1 (SSOT) / P2 (hot paths, measure-first)
Owner: rendering-core

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
immediately — see the commits accompanying this ticket.

## 1. Two CUDA/NV12/NVRTC authorities (P1)

Two independent implementations of the same CUDA/NV12 pipeline exist:

- **Authority A** — `src/backends/vulkan/cuda_nv12_surface_compositor.cpp`
  (used when CUDA interop is active on the Vulkan backend): own NVRTC
  load/compile cache, own PTX mutex, own module lifecycle.
- **Authority B** — `src/media/video/cuda_direct_nv12_compositor.cpp`
  (Direct-YUV path): the same responsibilities implemented a second time.

Both are live (not dead code). Risk: semantic drift between the two — color
conversion, blend semantics, NVRTC flags, architecture selection and error
handling must be remembered twice.

### Required before unifying (do NOT skip)

- Build an equivalence harness that renders the same NV12 scene through both
  authorities (DirectYUV and Vulkan) and differs outputs pixel-for-pixel
  (small tolerance for scheduling nondeterminism).
- Benchmark both on the reference GPU before/after the refactor.
- Only then extract a single `NvrtcBuiltins`/NV12 kernel authority and make
  both paths consume it.

Until then both files stay as-is; do not "clean up" one side cosmetically —
that is what creates drift.

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
