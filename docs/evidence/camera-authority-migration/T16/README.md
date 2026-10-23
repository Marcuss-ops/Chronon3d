# T16 camera GPU evidence

- `scene.camera.plan.json`: 150-frame / 30 fps camera plan lowered through CameraRig.
- `t16_hero_title_camera_150f_preview.mp4`: full 854x480 Vulkan preview rendered on NVIDIA RTX A4000.
- `t16_camera_contact_sheet.webp`: frames 0, 30, 75, 120, 149 extracted from the encoded preview.
- `t16_hero_title_camera_150f_fullres.mp4`: 150-frame 1920x1080 Vulkan/NVENC render on the strict GPU-native path.
- `scene.camera.three_depth.plan.json`: full-resolution variant with a CameraRig-driven background plane at z=+1000, title at z=0, and subject at z=-99.
- `t16_hero_title_camera_three_depth_150f.mp4`: 150-frame 1920x1080 Vulkan/NVENC strict GPU-native render of the three-depth variant.
- `t16_camera_three_depth_contact.webp`: frames 0, 30, 75, 120, and 149 from the three-depth video.
- `t16_camera_three_depth_random_frame_075.webp`: random-access frame 75 rendered separately with the same Vulkan/native-NVENC backend.
- `t16_camera_three_depth_raw_frame_075.webp`: non-compressed Vulkan raw-sink frame 75 projected verbatim to PNG by `tools/raw_frame_to_png.py`; visual review confirms the title and foreground cutout render correctly. Against decoded sequential/random NVENC frame 75, RGB MAE is 1.49/1.41, p99 5/4, max 31/27. This is pre-encode pixel-truth evidence for TEXT-QUALITY-V2 P0-0b; the differences include H.264 and chroma conversion.
- `output/T16_Hero_Title/`: source image assets needed by the plan. The text title is a transparent raster plane so it retains correct facing in the perspective renderer.

Validate from the Chronon3d repo root with:

```bash
build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli validate \
  --plan docs/evidence/camera-authority-migration/T16/scene.camera.plan.json \
  --assets-root docs/evidence/camera-authority-migration/T16 --profile preview
```

The full-resolution and three-depth base camera renders now exist. In the reviewed video, the title centroid moves 98 px from frame 0 to 149 while the subject-face centroid moves 32 px; the title/background gradient feature separation changes by about 68 px. The camera sample moves smoothly and the background remains in frame. Separately rendered frame 75 differs from the encoded sequential frame 75 by MAE 0.56 RGB levels (p99 5, maximum 19), accounting for H.264 encoding. This closes the base-resolution, strict GPU-native T16 render and random/sequential access check for frame 75. CAM-05 DoF now passes the strict-native GPU A/B; selective lighting, matte-edge quality, and shutter parity remain open.

SHA-256 manifest (the RenderPlan content digest also fingerprints its resolved assets):

```text
fedde0a8c27ebe75976672df5306ee8a3fd5b5a7e2411707246c6d0779312f30  scene.camera.plan.json
30b8d220a2ed811db315b9ed51a85f7f480624f9cd44c79be7e84c4efe766d6a  t16_hero_title_camera_150f_preview.mp4
9875cda366aaacb76fe60059d47b20a0d17652dcfdba52d6f0656b57044182a4  t16_camera_contact_sheet.webp
eb6c1c31a30228cde58242e92a2de87bed8cfd5acc3507d977a8c556907f212d  scene.camera.three_depth.plan.json
be70c71e857a71480864a1d01b9bb0338466a60a5568887e065e35d3c69c7c1e  t16_hero_title_camera_150f_fullres.mp4
67c53d3c780d98f5182edfabc180522c972075282d5d5887d121ab21e4eff26b  t16_hero_title_camera_three_depth_150f.mp4
94e44cf3036587e06eff53e701e3e7c62989c302c79d81b09074ef7d257a287c  t16_camera_three_depth_contact.webp
739c4856ee00d438aefc5848444418ea3d6211e351d19f1c423b330e0d4f4daf  t16_camera_three_depth_random_frame_075.webp
366f468c2510aa4c70b8bb4d90e58cf0364bb025ad0a9a99fd89b28a52265b3c  t16_camera_three_depth_raw_frame_075.webp
6a5d2d7459fd0d18665dc0d6080c9b1ccabbe858643aa2f0eb92ee3ecb2f4b3e  output/T16_Hero_Title/camera_background.png
ec2f7be0161b71c737bd8c0380eecadce5726db73e6c9a42c8fb2e466e102afa  output/T16_Hero_Title/camera_title.png
d4abade3bdcbc47d214feaf53c3fc39cbd09c4c245f77e6bb962d44cbc6429a0  output/T16_Hero_Title/subject_cutout_refined.png
```

Source identity (git trees were locally modified during this task): Chronon3d `a204d0d6c`; ChrononMotion3D `a460d9d013eabcb55cfec53ae105da866e3c242f`; ChrononTemplate `3872349`. Relevant source SHA-256:

```text
3d08ac24b1351b54effc11c9b61caca12f2da8d47a60677cd1509ad0581db5b0  ChrononTemplate camera-roll camera_plan_bake.cpp
259d48d7e11d4d853d4d4e02c75369ec19addf9f7f7453bf35dfc020e49deab4  ChrononMotion3D/include/chrononmotion/motion/CameraRig.hpp
9cb620e02ed8b7c4f585b6ef4fb709a580ba45dd647bce3bc119dd4aa879bd88  ChrononMotion3D/src/chrononmotion/motion/RenderPlanLowering.cpp
12941356dc58b497fcbee4ab1ae0ec9d99950f85b4810e604da3c4d8a1aaaf78  Chronon3d/src/render_plan/render_plan_compiler_scene.cpp
47664228c84d82ff7a2767d2de0b96605338c2b840e65fb2831ceba682a3f1fc  Chronon3d/src/render_plan/render_plan_compiler_animation.cpp
```


## CAM-01 FOV ablation (2026-10-21)

Pixel-truth raw-sink renders at frame 75 compare 30° and 50° on both software and Vulkan. The framing change affects 94.79% of RGB pixels (MAE 62.38 software, 62.37 Vulkan). Cross-backend MAE at a fixed FOV is 0.081 at 30° and 0.131 at 50°. Evidence and exact commands are in `fov-ablation/README.md`. The first still-image-lane experiment was invalid for pixel truth and is retained with an explicit `_still_lane_non_truth` suffix. CAM-01 and CAM-02 pass their focused pixel-truth tests. Threepp parallax and selective-light gates now also have focused GPU evidence in the cinematic certification ticket. Chronon3d/threepp pixel parity and shutter certification remain open. CAM-05 DoF passes the strict-native Vulkan/NVENC test documented below.


## CAM-08 raw access-order check (2026-10-21)

The complete 150-frame three-depth plan was rendered with Vulkan to the raw RGBA sink, then frame 75 was rendered in a separate random-access raw job. The extracted frames are byte-identical: RGB MAE 0, p99 0, max 0; each has content_fraction 0.837132. See `t16_three_depth_raw_random_sequential_contact.webp`, `t16_three_depth_sequential_150f_vulkan_auto.raw.rgba`, and `t16_three_depth_random_075_vulkan_auto.raw.rgba`. The strict `require_gpu_native` plus raw-sink attempt failed at the raw writer on frame 1; the normal Vulkan raw-readback path completed. The separate strict Vulkan/NVENC 150-frame deliverable is unaffected.

The 2026-10-21 DoF ablation first exposed a failure in the default raw Vulkan
readback route: it returns zero RGBA when DoF consumes CPU depth. A later strict
`require_gpu_native_full_graph` Vulkan/NVENC retest uses the existing native
depth-surface path successfully for 150 frames. CAM-05 passes for that strict
GPU composition; its five-frame visual A/B and ROI measurements are in
[`dof-ablation/README.md`](dof-ablation/README.md). The raw-readback failure
remains a separate backend-route limitation.

## GPU resource sample (2026-10-21)

Re-rendered `scene.camera.plan.json` for all 150 frames using Vulkan,
`require_gpu_native`, native `h264_nvenc`, and NV12. The output
`t16_gpu_resource_150f.mp4` decodes to 150 frames at 1920×1080/30 fps and has
the same SHA-256 as the previously reviewed `t16_hero_title_camera_150f_fullres.mp4`.
Its timing sidecar reports `required_gpu_native_full_graph`, 150 native encode
frames, and zero GPU readback bytes. A 200 ms `nvidia-smi` poll observed 993 MiB
maximum VRAM attributed to the render process and 66% peak device utilization;
the raw poll and summary are `t16_gpu_resource_samples.json` and
`t16_gpu_resource_summary.json`. The observed process value is a sampled peak,
not a guaranteed allocator high-water mark.

## Chronon3d software/Vulkan pixel parity (2026-10-21)

The same 1920×1080 three-depth RenderPlan was rendered for its full 150-frame
timeline through the Chronon3d software and Vulkan raw sinks. Each output has
exactly 1,244,160,000 bytes (150 complete RGBA8 frames); Vulkan device timing
is present for every frame. Per-frame RGB MAE averages 0.148636, the maximum
per-frame MAE is 0.162768, and the maximum per-frame p99 is 3 levels. On average
14.621% of pixels differ in at least one channel, predominantly by only a few
levels. The five required visual samples (0/30/75/120/149) were reviewed side
by side in `t16_cam04_full150_contact.webp` and retain matching perspective,
title/subject occlusion, and framing. Per-frame metrics are in
`t16_cam04_full150_metrics.csv`.

The raw sequences are `t16_cam04_full150_software.rgba` and
`t16_cam04_samples_vulkan.rgba`; logs and timing sidecars are beside them. The
Vulkan command used `--frames 0-120x15`, but the canonical plan route rendered
its complete 150-frame timeline; the output size and per-frame sidecar verify
that. The software run used the same plan without a frame override. This closes
Chronon3d software/Vulkan parity for this plan and timeline. It does **not**
close CAM-04, which requires pixel parity between the same camera RenderPlan
rendered by Chronon3d and threepp; the threepp camera-truth lane is separately
authored. The single-frame raw parity captures at frame 75 are retained as
`t16_cam04_frame075_{software,vulkan}.*`.


The focused threepp camera-truth GPU run, its five samples, and measurements
are preserved in [`threepp-camera-truth/`](threepp-camera-truth/README.md). It
closes the focused threepp parallax and selective-light gates, but not the
Chronon3d/threepp pixel-parity gate.
