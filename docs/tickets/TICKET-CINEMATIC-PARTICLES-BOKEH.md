# Cinematic particles and bokeh environment

## Scope

Define a deterministic environment-particle contract for ambient dust and
defocused bokeh before adding a render-plan decoder or backend implementation.
This ticket is deliberately a contract gate; `particle_emitter` is currently
accepted only by the shape/layer parser paths that already lower it.

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

## Status

`CONTRACT PROPOSED` — implementation has not started. Owner: Chronon3D scene
and backend maintainers. No new dependency or compatibility bridge is planned.
