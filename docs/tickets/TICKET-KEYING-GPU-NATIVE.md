# Luma/Chroma Key: native GPU support

## Current behavior

`matte.luma_chroma_key` is registered and implemented in the software effect
processor. It supports luma and RGB-distance chroma modes, soft thresholds,
alpha-preserving compositing, and spill suppression. Render-plan decoding,
validation, fingerprinting, and a deterministic canary are part of the same
local implementation.

Luma mode keys out dark pixels and retains brighter ones using linear Rec. 709
luma. Chroma mode keys pixels near `key_color` using normalized linear RGB
distance. `threshold` begins the transition, `softness` ends it, and `spill`
pulls partially keyed edge colors toward gray.

Keying now lowers to the existing Vulkan color-matrix pipeline for strict
GPU-only surfaces. A local Vulkan pixel-parity fixture covers luma/chroma,
spill, clip/choke, garbage matte, partial clip and partial alpha; strict
execution uses no software fallback. This is not same-SHA GPU-hardware
certification.

## Exit criteria

- [x] Implement native GPU luma/chroma processing with linear-light,
  premultiplied-alpha semantics matching the software processor.
- [x] Add local Vulkan/software parity coverage for thresholds, partial alpha,
  spill reduction, matte controls and clipped rendering.
- Add a GPU framehash canary and advertise the capability through backend
  resolution; run same-SHA GPU-hardware certification.

## Status

PARTIAL — native route and local Vulkan/software parity are implemented. The
GPU framehash canary, same-SHA GPU-hardware certification and RenderingGen
integration remain open.
