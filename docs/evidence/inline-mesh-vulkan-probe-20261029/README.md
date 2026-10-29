# Inline textured mesh Vulkan probe — 2026-10-29

## Provenance

- Chronon3D `HEAD`: `014eeb4dfd1703820d49b6382d127ab253bf1449`, dirty worktree.
- CLI: `build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli`.
  Binary SHA-256:
  `ac1527df04cede55e2f805d1699d8dda6f4dbd7e4a4c85c900e31d3857b630a7`.
- Test preset: `build/chronon/linux-video-test`; the focused binaries predate
  this run and are newer than the inline-mesh test sources. No test build was
  run during this probe.
- Device: NVIDIA RTX A4000. Before rendering: 9,277 MiB used, 6,700 MiB free,
  0% GPU utilization. No OOM occurred.

## Software and ABI checks

- `./build/chronon/linux-video-test/tests/chronon3d_c_abi_tests --test-case='*inline mesh*' --no-skip`
  — exit 0, 7/7 cases, 110/110 assertions.
- `./build/chronon/linux-video-test/tests/chronon3d_c_abi_tests --test-case='V3 parametric mesh primitives expand deterministically within the raw mesh contract' --no-skip`
  — exit 0, 1/1 case, 25/25 assertions.
- `./build/chronon/linux-video-test/tests/chronon3d_renderer_core_tests --test-case='*textured inline mesh*' --no-skip`
  — exit 0, 1/1 case, 4/4 assertions.
- `ctest --test-dir build/chronon/linux-video-test --output-on-failure -R '^(chronon3d_renderer_core_tests|chronon3d_c_abi_tests)$'`
  — exit 0, 2/2 CTest targets PASS (renderer core 13.70 s; C ABI 272.23 s).

These checks cover the software texture/alpha behavior, V3 decoding and
validation, aggregate mesh budgets, and deterministic primitive expansion.
They do not exercise Vulkan mesh rasterization.

## Strict Vulkan CLI probe

Command (exit 0):

```sh
CHRONON3D_VIDEO_PROGRAM_ROUTE=0 build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan /home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json \
  --assets-root /home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf \
  --backend vulkan --start-frame 0 --end-frame 0 --video-sink raw \
  --output /tmp/chronon3d-inline-vulkan.rgba -v info
```

- Timing sidecar reports `execution_path=full_graph_native` and
  `effective_backend=vulkan`.
- The raw artifact is one 640×360 RGBA frame (921,600 bytes), SHA-256
  `0b150fd32588b1daca5569992ebe559c0102c837306b1af4c44d35128ec58366`.
- Pixel inspection found 0 nonzero RGBA pixels, 0 nonzero-alpha pixels, and
  one unique value: transparent black `(0,0,0,0)`. The requested Vulkan render
  therefore completes without an error but does not draw the inline mesh.
- Stored visual artifact: [vulkan-frame.png](vulkan-frame.png), SHA-256
  `ce8b808ca0bd65bc8693edfff558f09e05a37dd8c65f63c49bfe2761c4e83d4b`.

## Current implementation boundary

`VulkanBackend::draw_node` currently tries grid, path fill, path stroke, and
solid-rectangle native paths; it has no inline-mesh dispatch. `SourceNode`
materializes a destination and calls `draw_node` for other shape types. The
software mesh processor has the existing indexed textured/alpha raster path,
but a software fallback is not Vulkan pixel proof. A strict-native triangle
draw path, this CLI pixel proof, and software/Vulkan parity remain open.
