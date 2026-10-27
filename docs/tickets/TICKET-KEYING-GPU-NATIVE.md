# Luma/Chroma Key: native GPU support

## TODO — cosa manca (agg. 2026-10-27)

- [ ] Certificazione same-SHA su host GPU (CI bloccato per billing; evidenza locale NVIDIA già registrata).

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

## Same-SHA WSL probe (2026-10-25)

At C3D `bb540c068`, `linux-video-test` built
`chronon3d_backend_registry_tests` and the keying selection passed 6/6 cases
(976 assertions), including software parity, stable frame hashes, and strict
Vulkan ordering. This WSL run selected `llvmpipe (LLVM 20.1.2, 256 bits)`;
CUDA interop was unavailable. Windows Vulkan enumeration sees the RTX 4060 Ti,
but it was not the device used for these tests, so this is same-SHA local
software-Vulkan evidence, not NVIDIA hardware certification.

## Windows NVIDIA hardware parity run (2026-10-26)

On the Windows host, `vulkaninfo --summary` selected the NVIDIA GeForce RTX
4060 Ti. The Release/Vulkan `chronon3d_backend_registry_tests.exe` target was
up to date, then the focused keying/Echo selection passed 8/8 cases and
1,009/1,009 assertions on that Vulkan device. The keying pixel comparisons
reported maximum absolute errors below `5.3e-8`; both luma and chroma display
framehashes exactly matched the software processor. This closes the local
NVIDIA keying parity check. The worktree contains uncommitted Vulkan changes,
so this run is not same-commit certification; the dedicated same-SHA gate
remains open.

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
