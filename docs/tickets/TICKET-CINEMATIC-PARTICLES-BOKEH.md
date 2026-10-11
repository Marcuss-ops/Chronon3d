# Cinematic particles and bokeh environment

## Scope

Add a bounded deterministic environment-particle mode with depth-driven bokeh.
The existing `particle_emitter` lowering is extended for software rendering;
native Vulkan parity remains a promotion gate.

## Proposed bounded contract

- Authored fields: `seed`, `count`, `bounds`, `depth_range`, `size_range`,
  `speed`, `turbulence`, `bokeh`, and a bounded color ramp.
- Hard budget: at most 4096 particles per environment and at most 64 MiB of
  canonical particle state per frame. Counts outside the budget fail closed.
- Determinism: particle positions derive from `(seed, particle_index,
  absolute_frame)`; no mutable RNG state and no dependence on worker order.
- Depth: store a normalized fake depth per particle; use it only for scale,
  opacity, and existing camera-driven DoF until a shared 3D environment model
  exists.
- Authority: software kernel defines placement, compositing, and frame output;
  Vulkan must provide a parity-certified replica or report
  `UnsupportedCapability`.
- Audio reactivity is excluded until the `ChrononMotion3D::OfflineBaking` PCM
  interface is available in this checkout.

## Acceptance gates

- Schema and decoder reject non-finite values, invalid ranges, and over-budget
  counts before fingerprinting.
- Fingerprint includes every authored field and seed.
- Random-access frame rendering is byte-identical across worker counts and
  renderer reuse.
- Software golden covers empty, full-budget, clipped, and bokeh-depth cases.
- Vulkan parity is tested on a Vulkan host before native promotion; otherwise
  the capability remains fail closed.

## Implementation status (2026-10-11)

`PARTIAL — SOFTWARE PATH IMPLEMENTED; GPU PARITY OPEN`.

- Emitter fields `environment`, `depth_range`, and `bokeh` are decoded in the
  V3 RenderPlan schema and validated. Environment emitters cap at 4096 samples
  and 64 MiB canonical sample state; ordinary emitters retain their existing
  limit.
- Depth is stateless and derived from seed/index/frame. Bokeh changes particle
  size and opacity, and environment depth adds deterministic scene placement.
- The V3 fingerprint includes the new fields while legacy plans retain their
  existing fingerprint bytes. V2 projection strips the V3-only fields.
- Software/Vulkan byte parity, a full-budget golden, clipping golden, random
  access worker-count parity, and native Vulkan fail-closed behavior still need
  implementation and verification. Do not mark this ticket complete until
  those gates pass on the required hosts.

### CPU sampler boundary re-run — 2026-10-11

The maintained `tests/scene/shapes/test_particles_sampling.cpp` source was
compiled as a standalone doctest binary against the checkout's xxhash library.
All **7/7 cases and 77,169/77,169 assertions** passed. The environment/bokeh
case now covers the accepted 4,096-particle boundary, checks the calculated
sample-state size against 64 MiB, and exercises empty emitters, bounded depth,
and deterministic random access across an intervening timestamp; 4,097 remains
rejected. This is sampler-level CPU evidence, not a rendered golden:
empty/full-budget/clipped pixel coverage, worker-count output parity, and
Vulkan parity remain open.
