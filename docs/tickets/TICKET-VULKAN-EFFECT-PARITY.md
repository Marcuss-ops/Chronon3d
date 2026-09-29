# TICKET-VULKAN-EFFECT-PARITY — Native GPU parity for the effect catalog

## Stato

OPEN — software exposes 41 effect types; strict Vulkan currently certifies four
native effect routes (Blur, Glow, Bloom, Tint) and explicitly rejects DropShadow.
All remaining types are unsupported on this capability surface. No CPU fallback
is allowed for a strict GPU request.

## Obiettivo

Every effect accepted by the canonical software effect catalog must have a
semantically equivalent native Vulkan implementation, or remain explicitly
rejected until its implementation and parity gates land. “GPU required” must
never be satisfied by software execution. Keep the existing EffectCatalog,
RenderGraph, GraphExecutor, surface authority and effect parameter contracts;
do not add a second renderer, registry, cache, or effect API.

## Contratto di parità

- Preserve each CPU effect's input/output alpha, color-space, edge, bounds,
  clipping, temporal and parameter-clamp semantics.
- Spatial and multi-input effects use only graph-owned native surfaces and
  planned scratch; no host readback or hidden CPU fallback.
- Animated effects sample the existing frame/time context and remain
  deterministic for the same inputs and frame.
- Strict native execution fails closed before dispatch if any enabled effect in
  the stack is not certified for its exact parameter/input contract.
- Capability rows are exhaustive over `EffectType`; adding a catalog effect
  without a declared native state must fail a compile/test gate.

## Waves

1. **Native surface primitives and simple color effects** — exhaustive
   capability accounting; Brightness, Contrast, Saturation, HueRotate, Invert,
   Exposure, Fill, Threshold, and other one-input pointwise transforms.
2. **Blur/light effects** — DropShadow and full parameter parity for Blur,
   Glow, Bloom, DirectionalBlur, RadialBlur, Sharpen, and Vignette.
3. **Spatial transforms and stylize** — Offset, Mosaic, CornerPin, Twirl,
   Bulge, MeshWarp, TurbulentDisplace, WaveWarp, Ripple, ChromaticAberration,
   Halftone, Stroke and Fake3DWave.
4. **Generators and multi-input effects** — Noise, FractalNoise, Checkerboard,
   FourColorGradient, LightRays, LUT3D, Curves, Levels, Keying, DisplacementMap
   and Echo, subject to the existing graph input and temporal contracts.

Waves may be split into small tickets; each ticket owns its shader ABI,
fingerprint/capability changes, software-vs-Vulkan pixel oracle, and rejection
cases. The catalog-wide ticket closes only when every catalog row is native and
the full software/GPU effect corpus passes on the same SHA.

## Acceptance gates

- [ ] Compile-time/test census accounts for every `EffectType` exactly once.
- [ ] Each wave has parameter-boundary tests and software/Vulkan pixel parity
  fixtures for transparent, opaque, clipped, edge and ROI cases as applicable.
- [ ] Multi-effect stacks preserve authored order and parity.
- [ ] Temporal effects render stable frame hashes and never reuse static cache
  entries incorrectly.
- [ ] Native strict export demonstrates zero readback/staging and no CPU effect
  dispatch for the entire supported catalog.
- [ ] GPU-host same-SHA certification passes; a host without GPU reports
  `NOT RUN`, not PASS.

## Demolition debt

- **Owner:** render-graph and Vulkan backend maintainers.
- **Reason:** native GPU support is incomplete while software is the broader
  canonical effect implementation.
- **Exit condition:** all catalog entries pass the native and parity gates.
- **Equivalence gate:** same-input software/Vulkan pixel oracle plus full-stack
  strict-native export and zero-readback evidence.
- **Removal scope:** the temporary unsupported capability rows and any
  effect-specific CPU-only routing guards that become obsolete.
- **Status:** ACTIVE.
