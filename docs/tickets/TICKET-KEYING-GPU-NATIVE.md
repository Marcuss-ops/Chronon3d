# Luma/Chroma Key: native GPU support

Status: OPEN — local Vulkan/software parity verified; same-SHA GPU hardware
certification remains pending.

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
execution uses no software fallback. Stable luma and chroma display-space
framehash canaries match both the registered software processor golden and
fixed expected hashes on the local Vulkan device. This is not same-SHA
GPU-hardware certification.

## Local GPU evidence (2026-10-08)

`chronon3d_backend_registry_tests` built from `linux-video-fast-dev` (CHRONON3D_ENABLE_VULKAN=ON): 95/95 test cases pass with the default device; the 8 keying/echo-related cases pass again with `VK_ICD_FILENAMES` pinned to the NVIDIA ICD (RTX A4000, Vulkan 1.4). Working tree had unrelated uncommitted changes, so this is not a same-SHA result.

## Current Vulkan software re-run (2026-10-11)

`vulkaninfo --summary` reports `PHYSICAL_DEVICE_TYPE_CPU` and `llvmpipe`
(Mesa 26.0.8, LLVM 21.1.8). The current
`linux-video-test` `chronon3d_backend_registry_tests` binary passed
`--test-case='*key*'`: **6/6 cases, 976/976 assertions**, including strict
Vulkan/software parity, stable display framehashes and ordered keying in
native effect stacks. This verifies the current Vulkan software path only;
same-SHA NVIDIA hardware certification remains open.

## Exit criteria

- [x] Implement native GPU luma/chroma processing with linear-light,
  premultiplied-alpha semantics matching the software processor.
- [x] Add local Vulkan/software parity coverage for thresholds, partial alpha,
  spill reduction, matte controls and clipped rendering.
- [x] Add luma/chroma GPU framehash canaries and advertise the capability
  through backend resolution.
- [ ] Run same-SHA GPU-hardware certification (local NVIDIA evidence recorded above; CI blocked by billing).

## Status

PARTIAL — native route, local Vulkan/software pixel parity and luma/chroma
framehash canaries are implemented. Same-SHA GPU-hardware certification and
RenderingGen integration remain open.
