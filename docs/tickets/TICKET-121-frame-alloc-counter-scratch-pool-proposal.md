# TICKET-121 — Frame-Scope Scratch Pool Proposal (allocation audit)

| Field      | Value                                                    |
|------------|----------------------------------------------------------|
| Status     | OPEN                                                      |
| Date       | 2026-07-09                                                |
| Deciders   | Agent3 (Buffy), awaiting code-review sign-off             |
| Tags       | performance, hotspot, framebuffer, scratch-pool, hot-path |
| Related    | AGENTS.md §Cat-2 freeze (no new public API), AGENTS.md ADR-freeze (no new singleton/registry/resolver/cache without ADR), include/chronon3d/ sealing |

## Context

The software-renderer hot path currently allocates transient `Framebuffer` wrappers
at several sites per frame. From a `git log -n` heat-map + static read of `src/backends/software/`,
six candidate sites show full-size (w × h × sizeof(Color)) `Framebuffer` allocations
on the per-frame hot path:

- `src/backends/software/processors/effects/blur/radial_blur.cpp:171,219`  – 1–2 per apply_radial_blur
- `src/backends/software/processors/effects/blur/directional_blur.cpp`     – 3 sites (horizontal/vertical/generic)
- `src/cache/persistent_framebuffer_store.cpp:293,392`                    – per load miss
- `src/backends/software/renderer_buffer_ring.cpp:13`                     – ensure_capacity (low frequency)

At 1920×1080×16 bytes (8 MiB per FB), even a single render pass with radial + directional
blur allocates ~16 MiB of fresh heap per frame, releasing it back at the end of the
function. This taxes the system allocator / arena (`cache::FramebufferPool`,
`core::FramebufferArena`) and creates predictable GC-style pressure on the LRU bucket
that is supposed to be reserved for long-lived persistent framebuffers.

## Goal (Scope-Bound, this PR = Phase 1 only)

Phase 1: instrument.  Phase 2: design.  Phase 3: implement.
This ticket opens the proposal and lands Phase 1 only.

### Phase 1 (this commit)

- New TU `src/backends/software/internal/frame_alloc_counter.{hpp,cpp}` declaring
  `chronon3d::internal::FrameAllocCounter` with three accessors:
  `record_alloc(bytes)`, `current_total_bytes_thread_local()`, `reset_thread_local()`.
- Backing state is strictly `thread_local std::size_t`. No atomic, no mutex, no global.
- Single one-line wiring in `radial_blur.cpp:171` (logger-style increment BEFORE the
  `std::make_unique<Framebuffer>(w,h)` of the temporary blurred FB).
- Unit test `tests/backends/software/test_frame_alloc_counter.cpp` asserting
  thread_local isolation.

### Phase 2 (follow-up ADR — `docs/adr/ADR-NNN-scratch-pool.md` — gated on this PR)  <!-- drift-allow: stale-ref -->

Document a candidate `FrameScratchPool` interface (per-renderer-instance reservation,
RAII return, deterministic teardown). Submit ADR only after Phase-1 measurements
are available.

### Phase 3 (gated on the ADR acceptance)

Wire additional call sites incrementally, one PR per site, each adding the
counter call-site AND the pool-guarded allocation.

## Decision (Phase 1)

### Spec

- Header (BUILD_INTERFACE only): `src/backends/software/internal/frame_alloc_counter.hpp`
- Implementation: `src/backends/software/internal/frame_alloc_counter.cpp`
- Namespace: `chronon3d::internal`
- API surface (internal, NOT INSTALLED):
  - `static void record_alloc(std::size_t bytes) noexcept;`
  - `static std::size_t current_total_bytes_thread_local() noexcept;`
  - `static void reset_thread_local() noexcept;`
- Wire site: `src/backends/software/processors/effects/blur/radial_blur.cpp:171`
  (single line: `internal::FrameAllocCounter::record_alloc(static_cast<std::size_t>(w) * h * sizeof(Color));`
  immediately before `auto temp = std::make_unique<Framebuffer>(w, h);`)
- CMake: add `internal/frame_alloc_counter.cpp` to `chronon3d_backend_software` OBJECT
  library (TICKET-118 BUILD_INTERFACE pattern enforced by CMake).
- Test: register `tests/backends/software/test_frame_alloc_counter.cpp` in
  `tests/backends_software_tests.cmake` under the existing
  `chronon3d_add_test_suite(NAME chronon3d_backends_software_tests ...)`.

### Anti-non-goals

- No new public API in `include/chronon3d/`.  Header is strictly `src/backends/software/internal/`.
- No new singleton, registry, resolver, or global cache.  `thread_local` storage is
  not a singleton (per-thread zero-init only).
- No deterministic-regression in any golden test.  Counter is observability-only;
  it does not change the order or content of the underlying `std::make_unique` call.
- No release / debug branching; counter is always-on.  An `if constexpr` guard
  can be added later if a cost regression is observed.

### Why one wire site only?

- Smallest PR blast radius; golden_test continue to render byte-identical PNGs.
- Demonstrates the threading/instrumentation mechanism without disturbing pool
  semantics (`cache::FramebufferPool` cache-miss path is contract-heavy).
- The other sites (`directional_blur` 3 sites, `persistent_framebuffer_store`
  load path) will be wired in follow-up PRs once Phase-2 ADR is approved.

## Risk

- `thread_local` is implicitly zero-initialised on first use; no overflow on 64-bit
  until a thread has allocated `1<<64` bytes.
- The counter measures *bytes-allocated-events* cumulatively, not the live footprint.
  Operators reading the counter will see the running total since last reset.
- `radial_blur.cpp:171` may not execute per frame when no radial blur is configured.
  Counter will read 0 in those runs — that is correct.

## Closure lineage

- Phase 1 closes today once `FrameAllocCounter` lands and `git diff range` is empty
  for any committed golden PNG (golden_hashes checker).
- Phase 2 ADR is opened once Phase-1 measurements expose a non-trivial figure
  (> N KiB / frame at 30 fps, threshold TBD).
