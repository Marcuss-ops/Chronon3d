# P0-07 alpha/cutout Vulkan end-to-end — 2026-11-04

This is a focused local + remote GPU check, not candidate-SHA CI certification.
The local CLI was built from the current worktree with the `linux-video-test`
preset and `-j12`; build base was `b1115868b` plus the source changes in this
worktree. Local CLI SHA-256: `62ab4617f9a6efbeb440182b73716c2afb7a6f68eff312c87c14cc43605a20d1`.
No remote build was run. The remote host is an NVIDIA RTX
A4000. The GPU fixture uses Vulkan with the auto GPU hot-path and raw RGBA
readback (no encoder/NVENC requirement).

Commands:

```sh
ninja -C build/chronon/linux-video-test -j12 chronon3d_cli chronon3d_camera_tests chronon3d_renderer_core_tests chronon3d_backend_registry_tests
/tmp/chronon-p0-venv/bin/python tests/e2e/test_p0_07_alpha_cutout_video.py \
  --cli build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli
python3 tests/e2e/test_p0_07_alpha_cutout_video.py --backend vulkan --cli ./chronon3d_cli_gpu
```

The complete 24-frame fixture passed on software and on the A4000 Vulkan path.
It checks opaque coverage, a moving alpha-cutout mesh, a fractional-alpha mesh
against an opaque reference, two planes at different depths, and a translucent
closed box. GPU results: zero cutout pixels below threshold; 32,400 pixels of
stable cutout coverage above threshold; fractional blend ratio 0.67 in sRGB
codes (the declared 0.5 linear-light blend); translucent box alpha 128/255.
Raw RGBA artifact SHA-256 values from the three GPU renders are recorded in the
run log: `43f016075fb5b2379cd2a217bf9ee39c738aeaa7b55fe8638d8ea52a775395ee`,
`7ff3848eee8449b430eafbe83af4a13df1cdc15ab78b3337aa666ad140a58e20`, and
`3b358fcdb55ba58edd04768de45ad661139ca0194488243fc151eee9c87445df`.

The Vulkan depth companion now records blend coverage with the reserved `-0.5`
marker without publishing it as an occluder; `-1` remains the no-coverage
sentinel, while non-negative markers retain opaque/DOF depth behavior.
