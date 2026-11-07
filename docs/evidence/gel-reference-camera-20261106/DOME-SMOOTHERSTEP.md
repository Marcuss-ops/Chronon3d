# GEL dome profile and fused debug flag — 2026-11-06

The CPU surface helper and Vulkan text tile shader now use the same smootherstep
dome, `h/r = 6u^5 - 15u^4 + 10u^3`, with slope `dh/du = 30u^2(1-u)^2`.
Both slope and curvature reach zero at the silhouette and medial axis. The
renderer unit test pins the height, midpoint normal, and numerical first and
second derivatives at both ends.

The Vulkan fused TextRun runner previously omitted both `gel_soft_fusion` and
`gel_debug_mode` from the packed run flags. The normal runner already carried
them. The fused path now packs the same fusion and three-bit diagnostic flags,
so these material settings behave consistently across graph dispatch routes.

## Evidence

- Diagnostic mode 2, transparent background, effects disabled:
  `gel-height-debug-transparent.png`
- Exact plan: `gel-height-debug-transparent.plan.json`
- Raw Vulkan RGBA8: `gel-height-debug-transparent.rgba`
- Raw SHA-256: `29ffe6b1ec0a6d6e8b3e0b3ea6b4b5377fd9026e4153207c7c1635a69ddd29b9`
- PNG SHA-256: `43b121e44115f8bcb8075868213a0be4e169bb9b91375e38982b68a18a26ed4e`

Reproduce the raw with:

```sh
build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/gel-reference-camera-20261106/gel-height-debug-transparent.plan.json \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 0 --video-sink raw --pipe-pixfmt rgba \
  --output /tmp/gel-height-debug-transparent.rgba
```

The debug frame was rendered with the `linux-video-test` Vulkan CLI and
`--gpu-hot-path-mode require_gpu_native`. Before the fused-runner flag fix, the
CPU `ctest` subset passed `chronon3d_text_tile_raster_canary_tests` and
`chronon3d_c_abi_tests`. After the fix, both Vulkan debug renders completed
with the rebuilt CLI and produced the height view. The renderer-core target
that contains `test_gel_surface.cpp` will be built and tested in the final gate.

The ordinary calibrated reference render remains byte-identical to its prior
raw capture (`f44a8838…f3b7`). Therefore this iteration proves the profile and
diagnostic path are present, but does not claim a visible improvement against
the reference. The full optical match and moving-footage integration remain
open.
