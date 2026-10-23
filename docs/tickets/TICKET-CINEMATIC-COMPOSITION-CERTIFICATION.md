# Cinematic composition certification — T16–T18

## Observed result (2026-10-19)

The RenderPlan → Chronon3d path now produces an actual three-layer T16 still
at 1920×1080: procedural night gradient, glowing title, and alpha cutout in
front of the title. The render was made by the video-enabled Chronon3d CLI on
the software backend, then extracted from its H.264 frame. Artifact and pixel
gate: [`output/T16_Hero_Title/README.md`](../../output/T16_Hero_Title/README.md).

The render exposed and fixed a correctness defect in early-exit analysis:
`ImageShape.opacity` was treated as proof that all decoded pixels are opaque.
Transparent full-canvas images could therefore retire the title/background
subtree below them. `SourceNode::can_seed_full_frame` now returns false until
the asset resolver can prove decoded alpha coverage. This is conservative and
may forgo an optimization for opaque full-frame images.

## Certification status

| Gate | Status | Evidence / remaining work |
|---|---|---|
| G01 Camera transport parity | PASS (focused) | Existing Motion → RenderPlan → decoder parity tests pass locally; same-SHA certification remains separate. |
| G02 Camera orientation parity | PASS (focused) | Camera orientation/projection focused tests pass locally. |
| G03 Rack-focus optical quality | PARTIAL | T10 measured ROI sharpness and rack focus, but quality/integration gate remains PARTIAL. |
| G04 Temporal camera determinism | PASS (focused) | T10 direct frame 77 equals sequential frame 77 byte-for-byte on the same backend. |
| G05 Mask temporal stability | PARTIAL | Matte and alpha edge behavior exist; representative video review and temporal edge gate remain. |
| G06 3D occlusion + alpha | PARTIAL | T16 still proves title behind a transparent subject cutout; moving depth/alpha composition remains untested. |
| G07 Dynamic light/shadow | PARTIAL | Existing light/shadow routes and tests exist; animated scene integration remains. |
| G08 Motion blur + DoF | PARTIAL | CAM-05 passes a focused strict-native Vulkan/NVENC DoF A/B on T16. Documentary Snapshot frame 30 and FocusDrop frame 53 visibly pass eight-sample, 90°-shutter Vulkan weighted-add probes through the FFmpeg/NVENC pipe. A CUDA RGBA→NV12 alpha-matte fix removes the brown field on strict-native SplitDepth frame 90; strict-native frame-30/FocusDrop rechecks and multi-frame moving-object blur certification remain. Evidence: [`GPU temporal probes`](../evidence/documentary-snapshot-recipes-20261021/gpu-torture-canary/README.md), [`FocusDrop review`](../evidence/documentary-snapshot-recipes-20261021/gpu-fullshutter-review/README.md). |
| G09 GLB material fidelity | PARTIAL | Import/render paths exist; trusted golden comparisons remain. |
| G10 RenderPlan produces pixels | PASS (T16 1920×1080 three-depth Vulkan) / PARTIAL (T17/T18) | T16 full-resolution and three-depth 150-frame videos each decode to 150 frames at 30 fps. |
| G11 Strict GPU path | PASS (T16 base composition) / PARTIAL (T17/T18) | NVIDIA RTX A4000, `require_gpu_native_full_graph`, native NVENC; zero conversion and staging-copy bytes in the T16 video. |
| G12 Resource/performance | PARTIAL | Representative GPU resource samples now exist for T16–T18. 200 ms `nvidia-smi` polling observed process-attributed VRAM peaks of 993/656/311 MiB and system GPU-utilization peaks of 66/28/11%; exact allocator high-water marks, same-SHA software/GPU comparisons, and full zero-copy accounting remain. |
| T16 Cinematic Hero Title | PARTIAL | CAM-01/02/03/04/05/06/07/08 pass focused gates; CAM-04 same-plan pixel comparison passes on five frames. The authored matte passes a four-background review and frame-75 90°/8-sample shutter parity passes decoded NVENC-vs-software (MAE 1.457, p99 5). Broader optical gates remain. |
| T17 Floating Product Orbit + Rack Focus | PARTIAL | RTX A4000/NVENC 150-frame four-product orbit has four measured focus peaks, exact frame-77 random access, and a 656 MiB sampled process-VRAM peak. The mapped GL-PBO→external CUDA AVFrame path has zero explicit CUDA D2D copies; composition approval and native C3D RenderPlan integration remain. Evidence: [`T17 GPU canary`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T17/). |
| T18 Documentary Crowd + Particle Reveal | PARTIAL | RTX A4000/NVENC 150-frame threepp canary passes crowd, animated matte, particle burst, shadow and random-access gates. The mapped GL-PBO→external CUDA AVFrame path has zero explicit CUDA D2D copies; RenderPlan/strict-native integration, temporal golden comparison, composition approval, and CPU-authored matte uploads remain. Evidence: [`T18 GPU canary`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T18/). |

## Next acceptance work

1. Add a regression test that keeps lower layers alive for a transparent
   full-canvas source and preserves the opaque-image optimization only when
   decoded alpha coverage is known.
2. For T16, review matte edges against approved reference material. The
   1920×1080/30 fps Vulkan/NVENC output, finite-depth environment plane, and
   frame-75 random/sequential access comparison are complete (raw pixel-truth
   frames match byte-for-byte).
3. Complete T17 ROI sharpness/occlusion gates and T18 temporal matte/particle
   gates using the existing certification registry and threepp path.
4. Record same-SHA GPU/software timing, memory, fallback and copy metrics for
   those end-to-end cases before marking them certified.

### Visual recheck of existing T17/T18 canaries (2026-10-22)

The archived contact sheets were inspected without launching another render.
In the T17 framing recheck, the avocado is clipped at the left edge by frame
149 (`evidence/camera-authority-migration/T17/framing-recheck-20261021/contact.png`).
In the T18 threepp canary, the moving subject becomes substantially occluded
by the green foreground figure at frame 120 and is reduced to a visible leg by
frame 149 (`ChrononMotion3D/out/threepp_certification/nvidia/T18/contact.png`).
These observations confirm the existing composition/occlusion blockers; they
do not identify a C3D renderer defect. The canaries still demonstrate motion,
focus and access-order behavior, but T17/T18 need shot/framing corrections and
RenderPlan/strict-native evidence before acceptance.

The certification-scene sources were adjusted in the shared worktree after
this review: T17's matched CameraRig/threepp FOV is now 48° (was 42°) to retain
edge margin; T18's CameraRig arc now ends at 1.4 rad (about 64°) so it stays on
the lead side of the crowd. Rebuild, GPU rerender, and frame review are pending;
the prior images above are pre-fix and do not certify the adjustment.

## T16 Camera Truth follow-up (2026-10-19; updated 2026-10-21)

A 150-frame T16 camera RenderPlan is authored at
[`output/T16_Hero_Title/scene.camera.plan.json`](../evidence/camera-authority-migration/T16/scene.camera.plan.json). The title card and alpha cutout are separate 3D image layers at world depths 0 and -100; the full-frame background uses the compositor backdrop path. CameraRig lowering produces 150 `camera_samples`, and C3D preview validation passes at 1920×1080.

The corrected 854×480 preview was rendered as one 150-frame Vulkan H.264
sequence on the NVIDIA RTX A4000 at 30 fps. The encoded file is
[`output/T16_Hero_Title/t16_hero_title_camera_150f_preview.mp4`](../evidence/camera-authority-migration/T16/t16_hero_title_camera_150f_preview.mp4). `ffprobe` confirms 150 decoded frames at
30 fps. Frames 0, 30, 75, 120, and 149 were extracted from that sequence and
visually reviewed in [`output/T16_Hero_Title/t16_camera_contact_sheet.png`](../evidence/camera-authority-migration/T16/t16_camera_contact_sheet.png). The title reads correctly, the subject remains composited in front of it, and both move smoothly. Pixel measurements from frame 0 to 149 show the subject centroid shifting 14 px and the title's left edge shifting 68 px in the 854×480 preview. The full-resolution 1920×1080 sequence was not rendered; this does not close T16.

The backdrop is screen-space, so the preview does not prove parallax across
three camera-projected world depths. It also does not establish
Chronon3d/threepp pixel parity, DoF, lighting selectivity, shutter behavior,
or random/sequential access parity. The separate high-resolution render
attempt was stopped after its measured 4–5 s/frame cost; the complete preview
sequence above ran in about 12.5 s.

The RenderPlan compiler now treats `focal_length_mm` as the projection
authority when it is authored, matching the threepp bridge, where
`setFocalLength()` recalculates the perspective FOV. Chronon3d uses a 35 mm
horizontal gate and derives the vertical gate from the output aspect ratio;
when no focal length is authored, `fov_deg` remains authoritative. This fixes
the static projection selection in the RenderPlan path. Pixel-level parity,
animated focal-length parity, camera parallax, and the T16 composition have
not yet been certified.

Before calling T16 a camera or cinematic certification, the scene contract is:

- **Subject and layers:** title, foreground cutout, and background/environment
  are separate layers at three explicit world depths, rendered through one
  perspective camera.
- **Camera and composition:** record position, target/orientation, near/far,
  output aspect, and exactly one optical authority (FOV or focal length plus
  sensor gate). The shot must visibly separate all three depth planes.
- **Motion and timing:** 150 frames at 30 fps; retain samples at frames 0, 30,
  75, 120, and 149. A camera orbit/push must yield measurable relative
  displacement between depth planes, not only uniform scaling.
- **Required evidence:** persist the RenderPlan, source SHAs, asset manifest,
  parameters, five sample frames, and a contact sheet. Inspect opening,
  middle, and ending frames before delivery.
- **Exclude:** a single still as proof of CameraRig, FOV/focal semantics,
  rack focus, temporal occlusion, motion blur, or GPU end-to-end behavior.
- **Preview approval:** first show an 854×480 (or closest supported) base
  composition through the perspective camera, before adding lighting/effects
  or rendering the full clip.

| Camera Truth gate | Required observation | Status |
|---|---|---|
| CAM-01 FOV 50° vs 30° | Measured framing changes | PASS (focused pixel-truth A/B) — 1920×1080 raw frame 75, MAE 62.38 software / 62.37 Vulkan; 94.79% pixels change. Evidence: `docs/evidence/camera-authority-migration/T16/fov-ablation/`. |
| CAM-02 camera dolly | Perspective scale changes by depth | PASS (focused Vulkan raw pixel-truth) — title scale ratio 0.6674×0.6659 vs predicted 0.6667; centered subject width ratio 0.6436 vs predicted 0.6431. Evidence: `docs/evidence/camera-authority-migration/T16/dolly-isolated/`. |
| CAM-03 camera orbit | Relative parallax among all three planes | PASS (focused threepp T16 truth scene: 1400.04 px spread across three anchors; 65.4763° orbit; RenderPlan composition matte/occlusion remains under G06) |
| CAM-04 Chronon3d/threepp | Same optical inputs align within a declared pixel tolerance | PASS (same serialized T16 RenderPlan; five 1920×1080 samples: RGB MAE 1.429–1.592, p99 20–21; limits MAE ≤1.70 and p99 ≤22). Evidence: [`CAM-04 same-plan comparison`](../evidence/camera-authority-migration/T16/cam04-shared-plan-20261022/README.md) |
| CAM-05 DoF ablation | Out-of-focus ROI sharpness changes when enabled | PASS (strict `require_gpu_native_full_graph` Vulkan/NVENC, 150-frame A/B: mean face ROI gradient decreases 19.95%, title ROI decreases 1.73%; 5 samples reviewed, 0 GPU readback/staging bytes, 0 downgrades. The separate default raw readback route still returns zero RGBA with CPU depth.) Evidence: `docs/evidence/camera-authority-migration/T16/dof-ablation/`. |
| CAM-06 lighting ablation | Only contracted light-receiving layers change | PASS (focused threepp T16 key ablation: title ROI 314,035 changed pixels; unlit portrait 36, below 62-pixel tolerance) |
| CAM-07 sampled animation | Five samples advance smoothly and follow CameraRig | PASS (150 CameraRig samples rendered; five GPU samples reviewed) |
| CAM-08 access order | Random-access and sequential frames match per backend | PASS (T16 frame 75 Vulkan raw pixel-truth: exact equality, MAE/p99/max 0; encoded comparison also remains documented) |

The corrected preview closes the authored-scene, sample-count, and basic
visual-motion portions. The 2026-10-21 full-resolution three-depth render
closes the T16 base video and strict GPU render portions. The 2026-10-22
focused matte review and encoded shutter pixel gate also pass for their tested
asset/frame. T16 remains `PARTIAL` pending same-RenderPlan Chronon3d/threepp
parity and broader dynamic-lighting certification. The focused raw Vulkan
shutter sink now returns valid pixels and matches decoded NVENC; see the
[readback root cause and evidence](../evidence/camera-authority-migration/T16/shutter-parity-20261022/raw-readback-fix/ROOT_CAUSE.md).
Focused selective lighting passes CAM-06; CAM-08 passes for T16 frame 75.

### Component validation run (2026-10-19)

- ChrononMotion3d focused CTest: `chrononmotion_camera_v1`,
  `chrononmotion_projection_parity`, `chrononmotion_render_pixel_certification`,
  and `chrononmotion_t16_camera_truth` passed (4/4). The diagnostic render
  measured FOV changes, >10° CameraRig orbit, three-plane parallax, selective
  title lighting, smooth sampled motion, and sequential/random-access frame
  equality. Five PNG samples were generated at 854×480 and 1920×1080.
- Chronon3d CTest: `chronon3d_motion_plan_adapter_tests` and
  `chronon3d_camera_tests` passed (2/2), including focal-length precedence and
  software DoF checks.
- These are component and diagnostic-scene results. Cross-renderer pixel parity
  and T16 DoF integration are not yet established. The subsequent T16 RenderPlan
  and video evidence are recorded in the 2026-10-21 closeout below.

### T16 full-resolution and three-depth GPU closeout (2026-10-21)

- Rendered the original 150-frame T16 plan at 1920×1080 with Vulkan,
  `require_gpu_native`, and native NVENC on the NVIDIA RTX A4000. `ffprobe`
  reports H.264, 1920×1080, 30 fps, and 150 decoded frames. The render took
  6.1 seconds end to end.
- Added `scene.camera.three_depth.plan.json`: the gradient background is a
  CameraRig-projected image plane at z=+1000, title at z=0, and foreground
  cutout at z=-99. Its strict-GPU Vulkan/NVENC video also contains 150 frames
  at 1920×1080 and 30 fps. The path resolves as `required_gpu_native_full_graph`;
  conversion and encoder staging-copy byte counts are both zero.
- Reviewed frames 0, 30, 75, 120, and 149 in
  `docs/evidence/camera-authority-migration/T16/t16_camera_three_depth_contact.png`.
  Pixel measurements across frames 0–149 show the title centroid shifting
  98 px, the subject-face centroid shifting 32 px, and a background-gradient
  feature shifting 30 px. The title's displacement relative to the far-plane
  feature is 68 px. This is measurable depth motion; matte-edge quality,
  optical DoF, lighting selectivity, and shutter parity remain uncertified.
- Rendered the three-depth plan sequentially for 150 frames through Vulkan's
  raw sink, then rendered frame 75 in a separate Vulkan raw job. `raw_frame_to_png.py`
  extracted the exact pre-encode RGBA8 frame from each artifact. They are
  byte-identical: RGB MAE 0, p99 0, maximum 0, and no differing pixels. Both
  images pass the content check (`content_fraction=0.837132`). Visual comparison
  is `docs/evidence/camera-authority-migration/T16/t16_three_depth_raw_random_sequential_contact.png`;
  raw source files and command logs are beside it. The earlier encoded comparison
  also remains valid (MAE 0.56, p99 5, max 19 after H.264). The first attempt
  to combine the raw sink with `require_gpu_native` failed at the raw writer on
  frame 1; rerunning Vulkan with the normal raw-readback path completed all 150
  frames. This sink limitation does not affect the separate strict NVENC video.
- Rebuilt the release CLI with `cmake --build
  build/chronon/linux-video-release --target chronon3d_cli -j16`. Focused
  `chronon3d_camera_tests` passed. The offline motion adapter test exposed
  stale expectations (35 mm instead of the RenderPlan's 36 mm gate, and an
  FOV-only case that retained focal length in baked samples); the fixture now
  matches the 36 mm contract and clears baked lens authority, and
  `chronon3d_motion_plan_adapter_tests` passes.

T16 remains `PARTIAL`; G10, CAM-08, and the T16-specific strict GPU path are closed.
CAM-01 now passes a pixel-truth FOV A/B on both software and Vulkan. The
initial still-image-lane comparison was invalid because that lane does not
compose all scene layers; its artifacts are retained and labeled as non-truth.
T16 remains partial until the other optical, matte, lighting, and shutter gates
pass.
T17/T18 and the broader certification remain open.

### Documentary Snapshot GPU temporal shutter probe (2026-10-21)

- Added Vulkan weighted additive accumulation with normalized weights and a
  job-persistent accumulator spanning the independent temporal sample batches.
  The accumulator is copied to the configured frame output before it is
  released.
- Rendered frame 30 of the original Documentary Snapshot torture plan with its
  authored eight samples and 90° shutter. Vulkan rendered and accumulated the
  frame on GPU; the FFmpeg/NVENC pipe output was visually reviewed and shows
  the intended portrait card, border, and caption. Evidence and command logs:
  [`gpu-torture-canary`](../evidence/documentary-snapshot-recipes-20261021/gpu-torture-canary/README.md).
- The same frame on native direct-surface NVENC accepts one frame but shows a
  large brown shape. The dedicated Vulkan-to-CUDA encoder copy and a follow-up
  with a reusable Vulkan sample surface and deferred encoder ownership handoff
  both fail identically. This rules out interleaved per-sample encoder handoffs
  as the cause; the direct-native output defect remains unresolved. Evidence:
  `strict_gpu_frame_030_deferred-handoff.mp4` and its reviewed PNG in the
  torture-canary directory. The full 601-frame recipe and moving-object blur
  certification remain open.
- Re-rendered FocusDrop frame 53 with its authored 90°/8-sample shutter and
  DoF enabled. Vulkan timing records 48 GPU submissions; visual review confirms
  readable title/layout and a defocused portrait. Evidence is in
  [`gpu-fullshutter-review`](../evidence/documentary-snapshot-recipes-20261021/gpu-fullshutter-review/README.md).
- Rechecked prior Vulkan frame-90 anomalies after renderer changes. `focus-drop`
  now shows its portrait, but remains heavily defocused with a partly clipped
  title; `foreground-photo-pass` has a dark background and an oversized clipped
  title. `split-depth` had been failing before output because DOF cloned a
  191×60 logical crop into a same-sized native surface while its source backing
  was 1920×1080. The clone now uses the registered source surface extent. GPU
  rechecks render visible split-depth content at diagnostic frame 90 and
  authored 90°/8-sample frame 53. This closes the black-frame symptom for those
  samples only; full-sequence and strict-native encoder certification remain
  open. Evidence is under `gpu-transition-review/` and
  `gpu-fullshutter-review/split-depth/`. A post-fix strict-native frame-53
  retry completes the eight-sample weighted add and NVENC accepts one frame
  with zero conversion/staging bytes, but the decode is still the oversized
  brown shape. The same brown ellipse appears in the earlier strict-native
  single-sample frame-90 controls for both FocusDrop and SplitDepth, whereas
  raw Vulkan frame 90 has only a subtle near-black radial falloff. Root cause was the CUDA RGBA32F→NV12 conversion unpremultiplying framebuffer RGB before writing a target with no alpha plane. The converter now preserves premultiplied RGB as compositing over black. The rebuilt strict-native full-shutter frame-90 render matches raw Vulkan background samples within 2 RGB code values (top `(27,14,2)` vs `(29,15,3)`, lower `(6,1,0)` vs `(6,2,1)`), with zero conversion/staging bytes. The strict full-shutter image remains softer around the face and title than the saved raw reference; DoF parity, other recipes, and full-sequence certification remain open. Evidence: `split-depth_frame090_strict-native_alpha-matte-fix.*` and its single-sample control.

### T16 native GPU DoF retest (2026-10-21)

- The earlier raw Vulkan readback DoF ablation remains reproducible as a
  zero-RGBA failure: that route supplies CPU depth to
  `VulkanBackend::apply_per_pixel_dof`, which requires a native depth surface.
  This result applies to the raw/readback execution path.
- Rendered the same DoF-enabled three-depth plan through
  `require_gpu_native_full_graph` and native NVENC for all 150 frames. `ffprobe`
  confirms H.264, 1920×1080, 30 fps, and 150 frames. The timing sidecar records
  150 native encode frames, zero GPU readback bytes, zero encoder staging-copy
  bytes, and zero execution downgrades. The video samples at frames
  0/30/75/120/149 were reviewed in
  [`t16_dof_on_strict_gpu_contact.png`](../evidence/camera-authority-migration/T16/dof-ablation/t16_dof_on_strict_gpu_contact.png).
- Compared decoded strict-GPU DoF-off and DoF-on videos over fixed ROIs.
  Mean face ROI gradient fell by 19.95%; the title ROI fell by only 1.73%.
  Frame 75 was also rendered as a matched one-frame strict-NVENC A/B. Full
  measurements and both outputs are in
  [`T16 DoF evidence`](../evidence/camera-authority-migration/T16/dof-ablation/).
- CAM-05 is now `PASS` for this focused T16 strict-native GPU composition.
  Its separate raw DoF readback limitation remains documented; T16 stays
  `PARTIAL` pending broader optical/lighting certification. CAM-04 same-plan
  sampled pixel parity now passes. Focused matte, lighting and shutter results are recorded in
  the 2026-10-22 closeout.

### T17 four-product GPU canary (2026-10-21)

- Added `chrononmotion_render_t17_product_orbit`, a four-GLB 150-frame,
  1920×1080/30 fps CameraRig orbit and rack-focus scene. It renders with the
  NVIDIA RTX A4000 OpenGL backend and submits/muxes all 150 frames through
  `h264_nvenc`.
- `ffprobe` confirms H.264, 1920×1080, 30 fps, and 150 decoded frames.
  Sequential and random-access frame 77 are byte-identical. Four subject ROIs
  peak at their corresponding focus samples; the weakest peak is 6.9% above
  its closest alternate (5% measurement tolerance). ROI coordinates are based
  on authored subject centers, not local GLB bounds.
- Reviewed the four GPU samples in [`contact.png`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T17/contact.png).
  All four assets render and the orbit changes their relative positions. The
  composition still has a black field, generous unused space, and insufficient
  separation for a presentation-quality/occlusion signoff. Diagnostic sharpness
  uses CPU readback outside the encode path; metadata records zero-copy as
  false and no in-process VRAM metric; the separate host poll observed 656 MiB
  attributed to T17 during the render.
- T17 remains `PARTIAL`: the GPU video, focus ordering, and deterministic access
  are evidenced; visual composition, occlusion, native zero-copy, and resource
  comparison against a same-SHA software reference remain open. Machine-readable results are in
  [`metadata.json`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T17/metadata.json).
- A framing iteration increased the fitted product span from 0.72 to 0.90
  units. The NVENC clip retains all four focus peaks and random/sequential
  frame-77 equality, while the 1.20-unit trial overlaps products and fails the
  focus diagnostic. Visual review of the retained iteration shows a larger
  presentation, but the avocado clips at frame 149 and substantial negative
  space remains. T17 therefore stays `PARTIAL`. See the
  [framing recheck](../evidence/camera-authority-migration/T17/framing-recheck-20261021/README.md).

### T18 crowd, matte, and particle GPU canary (2026-10-21)

- Added `chrononmotion_render_t18_documentary_crowd_particle_reveal`, derived
  from the existing animated low-poly crowd canary. The 150-frame,
  1920×1080/30 fps clip renders on the NVIDIA RTX A4000 and encodes through
  GL PBO → CUDA → NVENC. `ffprobe` confirms H.264 and all 150 frames.
- The integrated scene has a skinned CesiumMan lead, five procedural crowd
  mannequins, directional shadows, a deterministic 256×256 RGBA iris matte,
  and 48 individually depth-tested burst particles. The matte grows from 0 to
  65,536 transparent texels by frame 55; 46 particles are active at frame 45
  and none remain at frame 75. The frame-75 subject covers 66.4% of image
  height. Camera travel is 9.58 world units; bone-matrix delta is 64.84.
- Shadow toggles are deterministic at frames 75, 100, and 149; the shadow
  difference is concentrated on the floor. Random-access frame 77 is identical
  to the sequential render. The six-sample visual contact sheet is
  [`contact.png`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T18/contact.png);
  full video and JSON measurements are beside it.
- This closes a T18 integration canary on the threepp GPU renderer. T18 remains
  `PARTIAL`: this is not a connected RenderPlan/Chronon3d strict-native render,
  the matte texture is CPU-authored and uploaded each frame, zero-copy is not
  certified, the VRAM sample is not an allocator high-water mark, and no
  temporal golden comparison has been approved.

### T16–T18 host GPU resource sampling (2026-10-21)

- Re-rendered T16's 150-frame scene camera plan on the strict Vulkan/NVENC path.
  The resulting H.264 video is 1920×1080/30 fps, its timing sidecar reports
  `required_gpu_native_full_graph`, 150 native encode frames, and zero GPU
  readback bytes. Its SHA-256 exactly matches the earlier reviewed T16 fullres
  video. T16 sampled process VRAM peaked at 993 MiB and device utilization at
  66%; raw samples and summary are under
  [`camera-authority-migration/T16`](../evidence/camera-authority-migration/T16/README.md).
- During separate 200 ms `nvidia-smi` polls of T17 and T18, observed
  process-attributed VRAM peaks were 656 MiB and 311 MiB; device utilization
  peaks were 28% and 11%. Per-sample records and summaries live in each GPU
  canary directory. These are sampled peaks, not allocator high-water marks;
  system-wide VRAM deltas are noisy. G12 remains `PARTIAL` pending same-SHA
  software/GPU comparisons and complete per-stage copy/fallback accounting.


### T16 animated cross-backend parity (2026-10-21)

- Rendered the same 1920×1080 three-depth camera RenderPlan at animated frame
  75 to lossless RGBA raw on both software and Vulkan. Both PNG projections
  were visually reviewed and match in framing, title/subject occlusion, and
  composition.
- RGB difference is MAE 0.1551, p99 3, maximum 84; 99.42% of pixels are
  within four levels and only 60/2,073,600 pixels exceed 32 levels. With the
  thresholds MAE ≤0.25 and p99 ≤4, Chronon3d software/Vulkan parity passes
  on this representative moving-camera sample. This does not close CAM-04,
  which requires Chronon3d/threepp parity. Full 150-frame parity is not claimed
  by this single-frame capture; the complete comparison below supersedes that
  scope statement.
- Raw captures, PNG projections, logs, timing sidecars, exact plan digest and
  method are recorded in [`T16 camera evidence`](../evidence/camera-authority-migration/T16/README.md).


### T16 threepp camera truth and selective-light closeout (2026-10-21)

- Built and ran `chrononmotion_t16_camera_truth` at 1920×1080 under headless
  EGL. The run identified `NVIDIA RTX A4000/PCIe/SSE2`, passed, and saved
  contact frames 0/30/75/120/149 plus measured metadata at
  [`T16 Camera Truth`](../evidence/camera-authority-migration/T16/threepp-camera-truth/).
- CAM-03 now passes for this focused three-plane threepp scene: the CameraRig
  orbits 65.4763° and projected anchors have 1400.04 px parallax spread.
  CAM-06 now passes its selective key-light ablation: 314,035 title-ROI pixels
  change while the unlit portrait changes by only 36, within the 62-pixel limit.
  The camera path also stays below 0.15 world units per frame, random frame 75
  equals its sequential sample exactly, and BokehPass visibly racks focus.
- At this 2026-10-21 checkpoint CAM-04 remained OPEN: MotionBridge's 50 mm focal-length conversion matches
  the shared 35 mm gate calculation (22.2753° within 0.05°), but that is not a
  pixel comparison between the same RenderPlan rendered by Chronon3d and
  threepp. The separate Chronon3d software/Vulkan full-150-frame comparison is
  supplemental evidence only.
- The test target needed two build hygiene fixes: remove an unused `floor`
  binding and exempt stb's `missing-field-initializers` warning from `-Werror`
  for this target. The build and GPU render then completed successfully.


### T16 full-sequence Chronon3d software/Vulkan parity (2026-10-21)

- Rendered the same three-depth 1920×1080/150-frame RenderPlan through the
  software and Vulkan raw sinks. Both outputs contain exactly 150 complete
  RGBA8 frames (1,244,160,000 bytes); GPU timing is present for every Vulkan
  frame.
- Compared all frames before encoding. Mean per-frame RGB MAE is 0.148636,
  maximum frame MAE 0.162768, maximum per-frame p99 3 levels; 14.621% of
  pixels differ at least one channel, mostly by a small number of levels. The
  five required visual samples (0/30/75/120/149) were reviewed side by side
  and retain matching perspective, title/subject occlusion, and framing.
- Per-frame measurements are in `t16_cam04_full150_metrics.csv`; reviewed
  visual comparison is `t16_cam04_full150_contact.png`. Raw RGBA sequences,
  timing sidecars, and logs are retained beside them.
- This closes full-sequence parity between the Chronon3d software and Vulkan
  backends for this RenderPlan. CAM-04 was still open at this 2026-10-21
  checkpoint; it is superseded by the same-RenderPlan threepp pixel comparison
  recorded in the 2026-10-22 closeout below.

### T17/T18 camera reframe recheck (2026-10-22)

Rebuilt the edited threepp canaries with `ChrononMotion3D/build/release-fast`
and rendered both at 1920×1080, 30 fps, 150 frames on the NVIDIA RTX A4000.
The samples and MP4s are in:

- [`T17 reframe`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T17-reframe-20261022/)
- [`T18 reframe`](../../../ChrononMotion3D/out/threepp_certification/nvidia/T18-reframe-20261022/)

T17's matched CameraRig/threepp FOV is 48°. The avocado is inside the frame
through frame 149; all four focus ROIs follow their authored depth ordering,
and random-access frame 77 matches sequential rendering. The encode submitted
and muxed 150 frames. T17 remains `PARTIAL`: its diagnostic image review reads
back 151 frames, `gpu_resident_zero_copy` is false, and no native C3D RenderPlan
path or approved presentation-quality gate is connected.

T18's CameraRig arc ends at 1.4 rad. The lead subject remains visible in the
reviewed endpoint, and the 40-mannequin crowd, animated matte, particle timing,
shadow toggle, and random-access frame 77 gates pass. It also encoded and muxed
150 frames. T18 remains `PARTIAL`: RenderPlan/strict-native integration and a
temporal golden comparison are still absent. The 2026-10-22 direct-PBO canary
records zero explicit CUDA D2D copies, while retaining a framebuffer-to-PBO
transfer and CPU-authored matte upload. These renders verify the intended
camera corrections without closing the wider T17/T18 tickets.

### Camera optical and GPU encoder closeout (2026-10-22)

- **CAM-04 at this earlier checkpoint remained OPEN.** The 1920×1080 threepp recheck passes its 50 mm to
  35 mm-gate FOV conversion (22.2753°) and camera-truth gates; it still does
  not render the T16 RenderPlan, so it is not cross-engine pixel parity.
  Evidence: [`threepp camera truth recheck`](../evidence/camera-authority-migration/T16/threepp-camera-truth-recheck-20261022/).
- **T16 matte-edge passes for this authored cutout.** Its alpha was reviewed
  on black, white, magenta and checker backgrounds; transparent/partial/opaque
  coverage is 40.9245% / 1.7428% / 57.3327%. Review contact and metrics:
  [`matte-edge review`](../evidence/camera-authority-migration/T16/matte-edge-review-20261022/).
- **T16 frame-75 shutter parity passes its focused decoded-pixel gate.** The
  1920×1080 90°/8-sample plan uses Vulkan strict-native NVENC; decoded RGB versus
  the software shutter sample is MAE 1.457, p99 5 (limits 2 and 5). GPU
  shutter/no-shutter MAE is 0.271. This is one frame, not full-clip certification.
  The raw Vulkan sink now also produces a valid 8,294,400-byte frame on the
  same plan. Raw RGB versus decoded NVENC is MAE 1.414, p99 4; versus software
  center frame index 4 of the nine-frame reference it is MAE 0.143, p99 3.
  Minimal red-surface readback, no-shutter, 2-sample, and 8-sample checks pass.
  See [`shutter parity evidence`](../evidence/camera-authority-migration/T16/shutter-parity-20261022/)
  and its [raw readback root cause](../evidence/camera-authority-migration/T16/shutter-parity-20261022/raw-readback-fix/ROOT_CAUSE.md).
- **T05/T17/T18 encode-path copy gate closes in a scoped form.** The opt-in
  mapped GL PBO is passed as the external CUDA AVFrame to NVENC; each 150-frame
  GPU run muxes 150 packets and records zero explicit CUDA D2D frame copies.
  A framebuffer-to-PBO transfer remains, as do separate diagnostic CPU reads;
  T18 uploads its CPU-authored matte each frame. This does not close strict-native
  C3D RenderPlan integration or the broader scene tickets. Rechecks are in
  `ChrononMotion3D/out/threepp_certification/nvidia/{T05,T17,T18}-direct-pbo-20261022/`.

### Same-RenderPlan CAM-04 comparison (2026-10-22)

CAM-04 now passes its sampled pixel gate. Chronon3D and threepp consumed the
same serialized T16 RenderPlan and three image assets; frames 0, 30, 75, 120
and 149 were compared at 1920×1080 after normalizing OpenGL row orientation.
All samples meet RGB MAE ≤1.70 and p99 ≤22 (observed MAE 1.429–1.592, p99
20–21). The standalone verifier, per-frame metrics, raw samples, PNGs and
contact sheets are in [`CAM-04 shared-plan evidence`](../evidence/camera-authority-migration/T16/cam04-shared-plan-20261022/).
This closes the CAM-04 gate for the supported three-image-plane subset; it does
not close T16's broader optical/composition certification.
