# CAM-04 — same RenderPlan pixel comparison

Chronon3D software rendering and threepp rendered frames 0, 30, 75, 120 and
149 from the same serialized T16 `scene.camera.three_depth.plan.json` and its
150 resolved camera samples at 1920×1080. The threepp harness consumes the plan
and the same three image assets; it rejects unsupported layer types rather than
silently substituting a different scene. C3D raw samples come from the complete
150-frame RenderPlan render. The comparison converts threepp's OpenGL bottom-up
rows to C3D's top-down order and compares RGB.

## Acceptance limits and result

Per sampled frame: RGB MAE ≤ 1.70 and per-channel p99 absolute error ≤ 22 on
8-bit pixels. The limits tolerate rasterizer/texture sampling edge differences
while keeping broad color or framing errors outside the gate. All five frames
pass; measured MAE is 1.429–1.592 and p99 is 20–21. Maximum individual channel
difference is 114–147 and occurs in a small number of edge pixels. Exact per
frame hashes and metrics are in `verification.json` and `metrics.json`.

Run the deterministic pixel gate on the saved captures with:

```bash
python3 verify_cam04.py
```

The renderer source and CMake target live in
`ChrononMotion3D/render/tests/cam04_renderplan_pixels.cpp` and
`ChrononMotion3D/render/CMakeLists.txt`. The C3D 150-frame timing sidecar is
`c3d_render150_timing.json`; PNG samples and contact sheets are included for
visual inspection.

## Scope

This closes CAM-04's same-plan sampled pixel parity gate for the supported T16
three-image-plane subset. It does not certify every frame's pixel equality,
other RenderPlan node types, lighting, DoF, or the broader T16 composition.
