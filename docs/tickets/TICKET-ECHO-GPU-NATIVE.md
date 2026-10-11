# Echo effect: native GPU support

Status: OPEN — local native-route parity is covered; same-SHA GPU host
certification remains open.

## Current behavior

Strict Vulkan stores frame-addressed native copies in job-owned history and
dispatches Echo inside `EffectStackNode`, including as a stage in an ordered
native stack. Taps use weighted premultiplied source-over with no CPU readback.
History keys isolate streams; reset clears retained surfaces. The local Vulkan
integration fixture renders two consecutive frames and checks the decayed pixel
values. The backend-registry suite passes on llvmpipe.

Each invocation now clears only the trail accumulator. The current surface is
fully written by the source transform, and composed/output surfaces are fully
written by copy operations, removing three redundant full-frame clear passes.
The trail clear remains necessary because weighted taps accumulate into it.

## Local GPU evidence (2026-10-08)

`chronon3d_backend_registry_tests` from `linux-video-fast-dev` (Vulkan ON): 95/95 pass with the default device; the Echo cases (including `JobSession stores frame-addressed Vulkan Echo surfaces without readback`, `EffectStackNode composes Echo, brightness and chroma key over time`, and `Strict Vulkan rejects Echo without a native temporal surface history`) pass again with `VK_ICD_FILENAMES` pinned to the NVIDIA RTX A4000 ICD. Working tree had unrelated uncommitted changes, so this is not a same-SHA result.

## Current Vulkan software re-run (2026-10-11)

`vulkaninfo --summary` reports `PHYSICAL_DEVICE_TYPE_CPU` and
`llvmpipe (LLVM 21.1.8, 256 bits)`. On the current
`linux-video-test` `chronon3d_backend_registry_tests` binary,
`--test-case='*Echo*'` passed **4/4 cases and 215/215 assertions**, including
frame-addressed history, ordered effect composition and strict-mode rejection
without native history. This is current local software-Vulkan evidence only;
it does not satisfy the GPU-host same-SHA certification below.

## Exit criteria

- [x] Add native dispatch and job-owned history for Vulkan/GPU-only surfaces;
  Echo accumulation does not read back through the CPU.
- [x] Preserve exact frame addressing, history-key isolation, reset behavior,
  canvas-space bounds, and deterministic compositing order in the local path.
- [x] Add a Vulkan graph integration test with frame-to-frame pixel assertions
  for Echo → non-identity Brightness, compared channel-by-channel against the
  software processor and pinned by a second-frame display-space hash.
- [x] Expose the supported native route in the exhaustive effect capability.
- [x] Add a stable framehash canary and direct CPU software golden comparison
  to the two-frame Vulkan graph fixture; every output channel is compared and
  the second frame is checked against a fixed 8-bit sRGB FNV-1a hash.
- [ ] Pass GPU-host same-SHA CI certification; llvmpipe is local evidence only.

## Status

PARTIAL — native Vulkan Echo, local graph parity, framehash and direct software
golden comparison are implemented. GPU-host same-SHA certification remains
open. RenderingGen is out of this repository and must consume the native
capability.
