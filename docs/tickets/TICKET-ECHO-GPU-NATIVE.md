# Echo effect: native GPU support

## Current behavior

Strict Vulkan stores frame-addressed native copies in job-owned history and
dispatches Echo inside `EffectStackNode`, including as a stage in an ordered
native stack. Taps use weighted premultiplied source-over with no CPU readback.
History keys isolate streams; reset clears retained surfaces. The local Vulkan
integration fixture renders two consecutive frames and checks the decayed pixel
values. The backend-registry suite passes on llvmpipe.

## Exit criteria

- [x] Add native dispatch and job-owned history for Vulkan/GPU-only surfaces;
  Echo accumulation does not read back through the CPU.
- [x] Preserve exact frame addressing, history-key isolation, reset behavior,
  canvas-space bounds, and deterministic compositing order in the local path.
- [x] Add a Vulkan graph integration test with frame-to-frame pixel assertions.
- [x] Expose the supported native route in the exhaustive effect capability.
- [ ] Add a stable framehash canary and direct CPU software golden comparison.
- [ ] Pass GPU-host same-SHA CI certification; llvmpipe is local evidence only.

## Status

PARTIAL — native Vulkan Echo and local graph parity are implemented. The
framehash/software-golden and GPU-host same-SHA gates remain open. RenderingGen
is out of this repository and must consume the native capability.
