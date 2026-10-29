# T05 native Vulkan/NVENC acceptance — 2026-10-29

## Result: image gate PASS; shadow quality review remains open

The clean Windows Release build ran on an NVIDIA RTX 4060 Ti with strict
`require_gpu_native`, Vulkan and native NVENC. The baseline dispatcher allowed
parallel graph nodes and reproduced an incomplete caster-on frame in 1/5 runs:
mean RGB 5.0228, 94.3636% exactly black pixels. It retained the orange
rectangle but lost the ground plane and second caster although the CLI and NVENC
reported success.

The backend has one active frame-batch command buffer and shared command-pool
state. Vulkan requires external synchronization for concurrent access to these
objects. Graph nodes at the same level were recorded concurrently. The executor
now dispatches graph levels serially when the backend exposes native surfaces;
software backends keep the existing parallel path. This is the identified
cause of the intermittent loss, supported by the hardware A/B below.

With `CHRONON3D_CPU_RENDER_THREADS=12` (so the scheduler still had multiple
workers available), five fresh caster-on and five fresh caster-off frames were
rendered using the corrected binary. Every image passed the gate:

- Caster-on: mean RGB 52.1426, black ratio 0, opaque ratio 1.0 in each run.
- Caster-off: mean RGB 53.1802, black ratio 0, opaque ratio 1.0 in each run.
- Each A/B pair changed 11.3552% of pixels above the RGB delta threshold.
- All ten renders exited successfully and muxed one frame with native NVENC.

The five-on/five-off results plus the earlier 1/5 baseline failure strongly
support the concurrency diagnosis. They are not a Vulkan validation-layer
proof. The decoded A/B examples are [`caster-on-decoded.png`](caster-on-decoded.png)
and [`caster-off-decoded.png`](caster-off-decoded.png); the old failed image is
preserved as [`caster-on-degraded-decoded.png`](caster-on-degraded-decoded.png).

The pixel gate is [`run_t05_acceptance.py`](run_t05_acceptance.py). The exact
run logs and MP4s are retained in `build/chronon/win-release-clang` locally;
large run artifacts are not committed. Reproduce with the T05 production and
caster-off plans referenced by the ticket, strict Vulkan/NVENC flags, and
`CHRONON3D_CPU_RENDER_THREADS=12`.

## Other verification limits

The four targeted ownership/lifetime graph tests pass (46 assertions), and the
new serial-dispatch predicate test source compiles. The full graph suite last
reported 434/438: three overlay cases lack `assets/images/checker.png`, and
`test_velocity_buffer_motion_blur.cpp:149` crashed. The core suite did not
complete because `test_glyph_atlas_metadata.cpp` failed compilation with the
installed doctest setup. These suite issues and the visual shadow-quality
review are separate remaining gates; do not call the full T05 ticket closed
until they are addressed and the actual projected shadow is visually accepted.
