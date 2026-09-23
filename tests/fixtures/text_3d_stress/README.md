# Text 3D motion stress suite

The plans here are executable render-plan fixtures for the current Chronon3D contract. They exercise selector animation, 2.5D layer transforms, camera animation, depth compositing and the H.264 export path.

These are capability probes, not claims that every requested effect is fully implemented. The render-plan API currently exposes per-glyph 2D position/scale/opacity/tracking, 3D transforms at the layer level and camera position/rotation/FOV. It has no per-glyph 3D rotation, text-mesh deformation, extrusion/bevel controls, animated lights or depth-of-field focus track. The named `*_proxy` cases approximate those requests using the available controls and are marked as partial in the output manifest.

## Run

```bash
python3 Chronon3d/tests/fixtures/text_3d_stress/generate.py
bash Chronon3d/tests/fixtures/text_3d_stress/run.sh
```

The runner uses the built `chronon3d_cli` in the Linux video release tree and writes MP4s, timing summaries and logs to `ChrononTemplate/out/text_3d_stress_suite_v1/`. It validates every plan and checks that each export contains the requested frames.

Animated per-glyph text currently renders through the software backend. On this build, forcing Vulkan for a per-glyph animation fails with `UnsupportedCapability: VulkanBackend::draw_text_run: no legacy-node fallback attached`; camera-only and layer-depth Vulkan probes pass. This is recorded as a backend limitation rather than treated as a successful export.
