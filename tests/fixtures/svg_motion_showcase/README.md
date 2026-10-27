# Chronon SVG motion showcase

## Scene contract

- Subject: a fictional company growth infographic with a headquarters mark,
  four operating nodes, connector paths, a revenue arrow, a metric, and editable
  typography.
- Composition: 1920×1080, dark navy background, title in the upper safe band,
  diagram in the central field, metric in the lower safe band.
- Motion: 12 seconds at 30 fps. Reveal the hub and nodes, draw connectors with
  targeted Trim Paths, draw the revenue curve, and animate a scoped Repeater
  trail. The central mark has a topology-matched three-state SVG path morph.
  A restrained camera push-in runs on the same composition timeline.
- Sources: `company_growth.svg` contains the diagram and stable target IDs;
  `scene.plan.base.json` contains the camera and typography; `generate_plan.py`
  expands the deterministic camera motion into frame samples when rendering. The font reference uses
  the existing `tests/fixtures/Inter-Bold.ttf` fixture asset; its embedded
  metadata identifies the face as Roboto Bold. The repository bootstrap pins
  this test asset to the `google/fonts` snapshot in
  `tools/bootstrap_test_fonts.py` (snapshot `a31b162795d95c58e7a4e4d05df83398604487fb`,
  font blob `9d7cf220f98402e6908795046cd3c124886fc54a`).
- Exclusions: no real company identity, no application chrome, no rasterized
  text, no unrequested logos, and no replacement graphics.
- Preview acceptance: at 854×480, opening, middle, and ending frames must keep
  the diagram and labels inside frame, show the staged reveals, and retain a
  settled final metric without blank or transparent frames.

## Coordinates and camera

The SVG viewBox is 1920×1080 with SVG +X right and +Y down. Its root import
transform recenters the paths around the layer anchor and flips Y:
`x' = x - 960`, `y' = 540 - y`. The SVG layer is placed at canvas center
`[960, 540, 0]`. RenderPlan canvas positions use pixel units with the canvas
center at `[960, 540]`; the scene uses the existing perspective camera at
`[0, 0, -2600]`, looking along +Z with 30.219° FOV, zoom 2600, near 0.1 and
far 10000. Background sits at Z=160 and the diagram at Z=0.

## Rendering

From the repository root, run with an SVG and native FFmpeg enabled Chronon3D
CLI:

```sh
python3 tests/fixtures/svg_motion_showcase/generate_plan.py \
  tests/fixtures/svg_motion_showcase/scene.plan.base.json /tmp/scene.plan.json
chronon3d_cli render \
  --plan /tmp/scene.plan.json \
  --assets-root . --backend software \
  --encoder-backend pipe \
  --output outputs/svg_motion_showcase/company_growth.mp4
```

Keep the input SVG and plan alongside any delivered encode and contact sheet.

## Local rendered evidence

The current working snapshot was rendered through the software backend on the
encoded MP4 route. The output is `outputs/svg_motion_showcase/company_growth.mp4`:

- H.264, 1920×1080, 30 fps, 360 frames (12.000 seconds).
- FFmpeg 6.1.1 decoded the output; `ffprobe -count_frames` reported 360 frames.
- `outputs/svg_motion_showcase/contact_sheet.png` shows the opening at frame 12
  (0.40 s), midpoint at frame 180 (6.00 s), and final frame 359 (11.97 s).
  Full-size 854×480 review frames are in `outputs/svg_motion_showcase/review/`.
- SHA-256 of the decoded 854×480 RGBA review frames: frame 12
  `e17a24b56b86fdd7fb3b60ff8b8f19cdb0e343450c727109864726511c352812`,
  frame 180 `51a4bfa91c11237082efc94d0e88c322327d967c7bb141b43fdb6d492a12689f`,
  and frame 359 `45f42c2fe25cdb4e3e0397ed1e18967319ded056b89f7a8a2dd70dd4de8def04`.
- The local checkout HEAD was `208db389077fee43f80f0799c3e2d9c8e9e2438b` with uncommitted work.
  The exact scene plan SHA-256 is
  `db6d753f9595d4109600c0e58f8a9039cdb67f26ad0fef9941481fbf754516fa`;
  the SVG SHA-256 is
  `3f841d737d285e9110574f73738c62a4b51d6194847cd4e18126426cb9c319ef`.

Two full renders through the in-process FFmpeg pipe encoder were compared after
decoding all frames: all 360 RGBA frames match exactly, and both MP4 files have
the same SHA-256. The renderer's 60-frame raw output comparison also matched
exactly. The earlier default native encoder route produced differing decoded
frames in the opening sequence; use `--encoder-backend pipe` for this
reproducible showcase. A clean-SHA CI run remains pending.

To repeat the complete current-tree gate (two 12-second renders plus full
decoded-pixel comparison), run:

```sh
python3 scripts/ci/check_svg_motion_showcase_determinism.py \
  --cli build/chronon/svg-roadmap-verified/apps/chronon3d_cli/chronon3d_cli
```

## User delivery — 2026-10-18

The current checkout was rendered through the software backend and in-process
FFmpeg H.264 pipe encoder to the requested `Altri_video` folder. The delivered
file is `Chronon3D_SVG_Motion_Showcase_1080p30.mp4` (6,461,015 bytes):
1920×1080, 30 fps, 360 frames, 12.000 seconds. Its SHA-256 is
`9bd9f2421b12ae56a69b452d6b7f2f56edc426c87aa822763f6d41db3204efbe`. Two
complete renders have identical MP4 hashes and identical decoded RGBA pixels
across all 360 frames. The contact sheet and review frames 12, 180, and 359 are
delivered alongside it. An editable source bundle preserves the plan, SVG,
scene contract, and exact font fixture under repository-relative paths.
Clean-SHA CI remains outstanding.
