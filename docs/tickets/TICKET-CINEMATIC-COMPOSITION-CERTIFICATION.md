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
| G08 Motion blur + DoF | PARTIAL | Individual paths exist; combined moving-object/camera certification remains. |
| G09 GLB material fidelity | PARTIAL | Import/render paths exist; trusted golden comparisons remain. |
| G10 RenderPlan produces pixels | PASS (T16 still) / PARTIAL (end-to-end) | 1920×1080 T16 PNG has checked background/title/foreground samples; 150-frame T16 and T17/T18 compositions remain. |
| G11 Strict GPU path | PARTIAL | T10 NVIDIA/EGL/NVENC lane passed; end-to-end T16 strict GPU and copy/fallback accounting remain. |
| G12 Resource/performance | PARTIAL | T10 timing and peak RSS recorded; VRAM and representative T16–T18 metrics remain. |
| T16 Cinematic Hero Title | PARTIAL | One real Chronon-rendered 1920×1080 frame; 150-frame push-in, approved matte, edge and temporal gates remain. |
| T17 Floating Product Orbit + Rack Focus | PARTIAL | T10 rack-focus probe exists; four-product composition and presentation-quality ROI certification remain. |
| T18 Documentary Crowd + Particle Reveal | NOT RUN | Integrated crowd, animated matte, depth-sorted particles and full clip gate remain. |

## Next acceptance work

1. Add a regression test that keeps lower layers alive for a transparent
   full-canvas source and preserves the opaque-image optimization only when
   decoded alpha coverage is known.
2. Promote T16 from a still to the full 150-frame 1920×1080/30 fps plan and
   compare random-access frames with sequential output; review matte edges
   against approved reference material.
3. Complete T17 ROI sharpness/occlusion gates and T18 temporal matte/particle
   gates using the existing certification registry and threepp path.
4. Record same-SHA GPU/software timing, memory, fallback and copy metrics for
   those end-to-end cases before marking them certified.

## T16 Camera Truth follow-up (2026-10-19)

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
| CAM-01 FOV 50° vs 30° | Measured framing changes | OPEN |
| CAM-02 camera dolly | Perspective scale changes by depth | OPEN |
| CAM-03 camera orbit | Relative parallax among all three planes | OPEN |
| CAM-04 Chronon3d/threepp | Same optical inputs align within a declared pixel tolerance | OPEN |
| CAM-05 DoF ablation | Out-of-focus ROI sharpness changes when enabled | OPEN |
| CAM-06 lighting ablation | Only contracted light-receiving layers change | OPEN |
| CAM-07 sampled animation | Five samples advance smoothly and follow CameraRig | OPEN |
| CAM-08 access order | Random-access and sequential frames match per backend | OPEN |

This follow-up does not change the observed certification table above: T16
remains `PARTIAL` until the scene and evidence gates pass.

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
- These are component and diagnostic-scene results. The original T16 RenderPlan
  is still absent, cross-renderer pixel parity and T16 DoF integration are not
  yet established, and no full 150-frame video was rendered. Keep the overall
  T16 certification status at `PARTIAL`.
