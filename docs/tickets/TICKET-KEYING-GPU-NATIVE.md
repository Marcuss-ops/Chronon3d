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

GPU-only surfaces currently fail explicitly because there is no native keyer
dispatch. Do not silently omit the effect in a strict GPU render.

## Exit criteria

- Implement native GPU luma/chroma kernels with the same linear-light,
  premultiplied-alpha semantics as the software processor.
- Add shader/CPU parity tests for hard and soft thresholds, partial alpha,
  spill reduction, and clipped rendering.
- Add a GPU framehash canary and advertise the capability through backend
  resolution; strict GPU jobs must reject the effect at preflight until then.

## Status

OPEN — software path landed; native GPU path not implemented. RenderingGen is
outside this repository and has not been changed.
