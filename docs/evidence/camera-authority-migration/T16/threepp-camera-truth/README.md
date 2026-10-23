# T16 camera GPU truth on WSL — 2026-10-22

Focused 854×480 GPU run of `MotionScene → MotionSceneBridge → threepp OpenGL`
using Mesa D3D12 on the WSL NVIDIA RTX 4060 Ti. The output metadata reports
`D3D12 (NVIDIA GeForce RTX 4060 Ti)`; this is the device check for the run.
The contact sheet covers frames 0, 30, 75, 120, and 149, plus the 180°
shutter result. Full sample frames, ablation images, and machine-readable
measurements are in [`current-wsl-rtx-4060-ti/`](current-wsl-rtx-4060-ti/).

Run from `ChrononMotion3D/`:

```bash
cmake -S . -B build-t16-camera-truth \
  -DCMAKE_PREFIX_PATH=/mnt/c/Users/pater/dev/projects/Chronon3d/vcpkg_installed/linux-fast-dev/x64-linux \
  -DCMAKE_BUILD_TYPE=Release \
  -DCHRONON3D_INCLUDE_DIR=/mnt/c/Users/pater/dev/projects/Chronon3d/include \
  -DCHRONONMOTION_BUILD_RENDER=ON -DCHRONONMOTION_BUILD_TESTS=ON
cmake --build build-t16-camera-truth --target chrononmotion_t16_camera_truth -j8
GALLIUM_DRIVER=d3d12 THREEPP_GLFW_PLATFORM=egl \
  build-t16-camera-truth/render/chrononmotion_t16_camera_truth \
  /home/pater/build/chrononmotion-t16-gpu-4060ti 854 480
```

Focused GPU gate measurements:

| Gate | Result |
|---|---|
| CAM-01 FOV 50°→30° | PASS — 325,296 pixels changed at frame 75. |
| CAM-02 near/far perspective | PASS — projected width ratio 1.69014×. |
| CAM-03 orbit/parallax | PASS — 65.4763° orbit; 622.241 px parallax spread at 854×480. |
| CAM-04 50 mm lens authority | PASS for analytic 35 mm sensor-gate FOV — both paths report 22.2584°. The same-RenderPlan C3D/threepp image comparison remains open. |
| CAM-05 Bokeh DoF on/off | PASS — 237,852 pixels changed. RGB contact images are provided; no alpha channel is used in this camera-truth target. |
| CAM-06 selective key light | PASS — 61,837 title-ROI pixels changed; the unlit portrait ROI changed 9 pixels (25-pixel tolerance). |
| CAM-07 CameraRig motion | PASS — max step 0.0876953 world units; five required frames are distinct. |
| 180° shutter | PASS for the focused camera-only probe — eight exact CameraRig sub-frame samples across a 0.5-frame exposure change 43,664 pixels. See the shutter on/off and ×4 difference images. |
| CAM-08 random access | PASS — frame 75 matches its prior sequential sample; frame 135 is byte-identical after rendering frames 0–135 sequentially, SHA-256 `e1b12cac8aae847dd6a4d99206ff79adf9be3579c32e839ab4bab3c993a30059`. |

This is a focused threepp GPU canary, not a full video render and not the same
RenderPlan rendered by both engines. The shutter probe establishes that CM3D
CameraRig samples at sub-frame exposure times affect GPU-rendered pixels; it
does not establish Chronon3D offline-sample/runtime-provider parity. CAM-04's
same-RenderPlan parity and Chronon3D's strict Vulkan GPU render remain open.
On this WSL distro, NVIDIA is available through Mesa D3D12/OpenGL, while the
Vulkan loader selects llvmpipe. CUDA/NVENC availability alone is not evidence
of a Chronon3D Vulkan render.

The underlying portrait asset attribution remains CC BY 4.0; see the source
attribution recorded in `camera_truth.json`.
