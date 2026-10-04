# Echo effect: native GPU support

## Current behavior

Echo is available through the software effect processor. Render-plan decoding,
effect ordering, temporal history, fingerprints, and a deterministic
framehash canary are implemented locally. The effect needs access to earlier
full-canvas surfaces from the same layer/effect stream.

When execution receives a GPU-only surface, `EffectStackNode` fails explicitly
with `Echo is not available for GPU-only render surfaces`. It must not be
silently skipped or treated as GPU-native.

The native backend now exposes weighted premultiplied source-over compositing
for accumulating Echo taps without readback. This is a reusable primitive only:
frame-addressed GPU history, Echo stage integration, and the graph canary remain
unimplemented, so strict Vulkan still rejects Echo.

## Exit criteria

- Add a native effect dispatch and history storage contract for Vulkan/GPU-only
  surfaces; do not read back through the CPU unless the renderer's explicit
  fallback policy requests it.
- Preserve exact frame addressing, per-layer/effect isolation, reset behavior,
  canvas-space bounds, and deterministic compositing order.
- Add a GPU graph integration test and framehash canary; verify the software
  golden remains unchanged.
- Expose the capability to the backend resolver so GPU-required plans fail at
  preflight when Echo is unsupported.

## Status

OPEN — software path landed; native GPU path not implemented. RenderingGen is
out of this repository and must consume the capability once that integration
is available.
