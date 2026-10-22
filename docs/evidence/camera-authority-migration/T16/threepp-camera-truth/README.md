# T16 threepp camera truth GPU run

Rendered a focused `MotionScene -> MotionSceneBridge -> threepp OpenGL` camera
scene at 1920×1080 with headless EGL. `camera_truth.json` identifies the device
as `NVIDIA RTX A4000/PCIe/SSE2`. The five rendered frames 0, 30, 75, 120, and
149 were visually reviewed in `contact_sheet.png`; their lossless full-size
images are in this folder.

The run command, from `ChrononMotion3D/`, was:

```bash
THREEPP_GLFW_PLATFORM=egl build/release-fast/render/chrononmotion_t16_camera_truth \
  out/threepp_certification/nvidia/T16_camera_truth 1920 1080
```

Build command:

```bash
cmake --build build/release-fast --target chrononmotion_t16_camera_truth -j16
```

The camera orbits 65.4763°. Three world-depth anchors produce 1400.04 px of
parallax spread; the near/far projected width ratio is 1.69014. Maximum camera
translation per frame is 0.08770 world units. The contact sheet shows the
foreground portrait sharp at the opening, then out of focus while the extruded
title comes into focus as the camera sweeps through the scene.

Focused gate measurements from `camera_truth.json`:

- CAM-01 FOV 50°→30° changes 1,648,673 pixels at frame 75.
- CAM-02 perspective places the near plane 1.69014× wider than the far plane.
- CAM-03 parallax spread is 1400.04 px; camera orbit is 65.4763°.
- CAM-06 key-light off/on changes 314,035 pixels in the lit title ROI. The
  intentionally unlit portrait ROI changes 36 pixels, under the 62-pixel
  tolerance.
- CAM-07 the 150-position CameraRig path stays below 0.15 world units per
  frame; all five required visual samples are distinct and move smoothly.
- CAM-08 random-access frame 75 exactly equals the sequential frame-75 sample.
- MotionBridge's 50 mm focal-length path computes 22.2753° FOV, matching the
  shared 35 mm sensor-gate calculation within 0.05°.
- BokehPass visibly shifts focus from portrait to title.

The metadata reports `full_clip_rendered=false`: this proves the focused scene
and camera gates, but not a full encoded 150-frame clip. The truth scene is
separately authored, so it does not close CAM-04's required pixel comparison
between the same RenderPlan rendered by Chronon3d and threepp.
