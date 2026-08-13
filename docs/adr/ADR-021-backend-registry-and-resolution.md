# ADR-021 — Backend registry and explicit resolution

## Status

Accepted — 2026-08-13

## Context

Chronon already exposes a `RenderBackend` boundary, but backend choice was
implicit: runtime construction attached the software backend directly. A GPU
backend needs explicit `auto`, software, and strict GPU selection without
placing backend branches inside render-graph nodes.

## Decision

Use an instance-owned `chronon3d::graph::BackendRegistry` and
`BackendResolver`. The registry stores one descriptor and factory per
`BackendType`; it is not a singleton, process-wide service locator, or cache.
`BackendPreference::Auto` prefers Vulkan and falls back to Software, while
`BackendPreference::GPU` considers only Vulkan and returns a structured error
when it is unavailable or cannot satisfy requirements.

Backend capabilities and requirements remain backend-neutral. Vulkan-specific
objects and implementation details stay outside this contract.

Logical surface identity is likewise instance-owned: `RenderSurfaceRegistry`
allocates opaque handles and records descriptions/physical-slot bindings, while
the backend owns the actual storage.

## Consequences

- Software remains the reference and fallback backend.
- Strict GPU callers cannot silently degrade to CPU rendering.
- Logical surface identity can be planned and aliased without exposing
  `VkImage` or `Framebuffer` to graph-level contracts.
- Runtime/backend construction can be tested without a Vulkan device.
- The initial headless Vulkan device runtime, native logical-surface
  execution (source-over plus integer/affine transforms), and an engine-local
  digest-keyed GPU asset LRU, a central GPU-kernel registry, ticketed
  multi-slot staging uploads signaled by a Vulkan timeline semaphore, and a
  separable device-local blur pass and a one-submission H/V+Add glow batch are
  implemented behind `CHRONON3D_ENABLE_VULKAN`. A device-local color-adjust
  kernel covers the conservative full-frame Tint subset, and a three-image
  alpha/luma matte kernel covers aligned full-frame track mattes. The full-frame
  `EffectStackNode` dispatches this path while preserving the CPU fallback;
  its three pass descriptor sets are reused across submissions. Physical-device
  selection scores discrete GPUs above software/CPU Vulkan devices and exposes
  the selected device in backend stats. Transfer-queue separation and
  projective/effect passes remain follow-up work.

## Rejected alternatives

- A global backend singleton: creates hidden lifetime and test-isolation
  coupling.
- Backend branches in nodes: duplicates graph semantics and makes parity
  testing harder.
- Selecting by environment variable only: cannot express strict failure or
  per-runtime capability requirements.
