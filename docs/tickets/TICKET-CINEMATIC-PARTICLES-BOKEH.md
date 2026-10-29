# Cinematic particles and bokeh environment

## TODO — cosa manca (agg. 2026-10-28)

- [ ] **Parità software/Vulkan byte-identica** — logica: il software kernel è l'autorità di placement/compositing; il replica Vulkan deve essere parità-certificato prima della promozione, altrimenti la capability resta fail-closed (`UnsupportedCapability`). Dove: emitter environment nel lowering `particle_emitter` + dispatch Vulkan strict-native. Gate: test di parità byte su casi full-budget/clipping/bokeh-depth.
- [ ] **Golden + random access** — logica: golden software per empty/full-budget/clipped/bokeh-depth, e rendering random-access byte-identico su conteggi worker diversi. Dove: `tests/` (fingerprint + golden), schema V3 (`environment`, `depth_range`, `bokeh`). Gate: golden verdi + determinismo cross-worker.
- [ ] **Certificazione su host Vulkan** — logica: il promotion gate richiede l'esecuzione su host Vulkan reale (llvmpipe non basta per la promozione). Gate: run same-SHA su host Vulkan; fin lì fail-closed.

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
