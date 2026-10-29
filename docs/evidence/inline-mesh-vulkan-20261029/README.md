# Inline mesh Vulkan strict-native evidence (2026-10-29)

## Scope and source

Current checkout base SHA: `556a4ae58933d0c6b72eb22829b415e08ffc43d4`; the
working tree was dirty during this run. Build preset:
`build/chronon/linux-video-test` (`CHRONON3D_ENABLE_VULKAN=ON`,
`CHRONON3D_ENABLE_CUDA_INTEROP=ON`). Device: NVIDIA RTX A4000, driver
`595.99.02`. No concurrent build was running.

Fixture: the existing 31-sample animated textured/alpha glTF RenderPlan in
`../../../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/`.
The fixture is diagnostic and small (640x360); it is not the final documentary
content pack.

## Results

- Software raw render: **PASS**, 31/31 frames, 28,569,600 bytes.
- Strict-native Vulkan + native NVENC H.264: **PASS for route completion**,
  31 frames accepted, 31 packets muxed, no software fallback reported, and
  `ffprobe` decodes 31/31 frames at 640x360 and 30 fps.
- Determinism after the lighting fix: a second full render of the same plan
  produced the identical MP4 SHA-256:
  `9d3ad734bccdfbc09569088508a3e10fb3fb06152af637b13530c0ab10da717b`.
- Visibility: subject pixels are present in all decoded frames and move over
  the sequence. Contact preview: `inline-mesh-vulkan-contact.png`.
- Software/GPU appearance parity after the lighting fix: **PASS within the
  current CAM-04 image gate**. Both paths were exported with the same NVENC
  H.264 configuration and compared after decoding. Full-frame RGB MAE is
  0.0210; RGB MAE over the software subject alpha mask is 1.492 (p95 5,
  p99 8, max 17). Sample pixels at frames 0/15/30 are within 9/2/6 channel
  levels respectively. The prior mismatch came from the native path omitting
  the resolved scene light factor on material-bearing inline meshes.

The result uses lossy decoded MP4 frames rather than direct linear surface
readback, so it certifies this end-to-end fixture and codec route, not general
PBR conformance for every glTF material.

## Commands

Software NVENC reference:

```sh
build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
  --assets-root "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf" \
  --backend software --start-frame 0 --end-frame 30 --video-sink ffmpeg \
  --encoder-backend native --hardware nvenc --codec h264_nvenc \
  --pipe-pixfmt nv12 --output /tmp/chronon3d-inline-mesh-software-lit.mp4
```

Strict-native NVENC render (run twice; both output hashes matched):

```sh
build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
  --assets-root "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf" \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --video-sink ffmpeg --encoder-backend native --hardware nvenc \
  --codec h264_nvenc --pipe-pixfmt nv12 --start-frame 0 --end-frame 30 \
  --output /tmp/chronon3d-inline-mesh-vulkan-lit.mp4 -v info
```

The full backend registry CTest passed (1/1 CTest target; 119 test cases,
1,914,121 assertions; 128.92 s). Its first run exposed a stale expected kernel
registry size (24); after updating it to 25 and rebuilding, the suite passed.
The focused inline-mesh dispatch test passed (1 case, 23 assertions). The
aggregate payload budget test passed in the C ABI target (1 case, 8
assertions). Two full strict-native renders produced byte-identical MP4 files.

## Ticket disposition

Implementation, strict-native route, decoded frame count, foreground parity,
determinism, payload budget, focused GPU test, and full backend registry CTest
pass locally. The ticket remains **PARTIAL** until CI passes on the exact
pushed SHA; this checkout's working tree was dirty during local verification.
This is a small diagnostic fixture and does not certify every PBR, shadow, or
production-content combination.

## Current-source rerun after push (2026-10-29)

The Release CLI was rebuilt from the clean, pushed Chronon3D source at
`0e7303577524d8b1c1819213de022390cca6775e` after the
native terminal-surface copy fix. The same Motion3D producer fixture rendered
again through strict-native Vulkan + NVENC:

- `ffprobe` decoded 31/31 H.264 frames at 640×360 and 30 fps.
- The timing receipt reports `full_graph_native`, zero execution downgrades,
  zero CPU pixel readback, and zero encoder staging-copy bytes.
- Every decoded frame hash is distinct, and the resulting MP4 SHA-256 is
  `9d3ad734bccdfbc09569088508a3e10fb3fb06152af637b13530c0ab10da717b`, matching
  the earlier deterministic run.
- Evidence: `current-source-main-31f.mp4`, its `.timing.json` receipt, and
  `current-source-main-31f.log`.

The strict-native mesh integration is now locally verified on the pushed
source. The ticket remains **PARTIAL** only for the same-SHA CI gate and broad
production/PBR coverage beyond this fixture.
