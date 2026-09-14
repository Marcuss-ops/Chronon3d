# TICKET-GOAL3-NATIVE-COMPOSITE-COLLISION — a baked text layer hides a scaled video layer, and lands at the canvas origin

**Status:** RE-DIAGNOSED (third pass, 2026-09-14) — placement is FIXED; color parity and encoder determinism are implemented and await GPU validation
**Created:** 2026-09-14
**Scope:** `chronon3d_cli` GPU-native lane (`required_gpu_native_full_graph`)
**Reporter:** RenderingGen audit session (read-only; RenderGen tree untouched)
**Evidence artefacts:** `refactored/ops/benchmarks/goal3-e2e-<date>/` (RenderingGen repo, sibling checkout)

## Third pass — current state (2026-09-14)

- **R1 placement/banda nera: FIXED.** `TransformNode::predicted_bbox()` clips the graph
  bbox to the actual input surface before publishing the tight surface. The plate is now
  full-canvas (`[0,0→1920,1080]`) and the foreground is the centered `1344×756` box
  (`[288,162→1632,918]`). Pixel, watermark, video-composited and boundary checks pass.
- **R2 color: implementation complete, E2E pending.** Native NV12/P010 import applies the
  inverse transfer before publishing canonical `RGBA Linear/Identity/Full`; the software
  decoder applies the same inverse transfer before entering the framebuffer. The current
  host has no NVIDIA driver, so native/software MAE cannot be re-measured here.
- **R3 determinism: encoder contract hardened, E2E pending.** Goal3 pins NVENC to constant
  QP 23 and the software reference to explicit CRF 18; neither lane relies on an implicit
  driver-default rate-control mode. Re-run the full harness on a host with NVENC.

## Second pass — VERIFIED against the current engine (2026-09-14)

This section supersedes the hypotheses below. Re-run on a rebuilt
`chronon3d_cli` (release, `build/chronon/linux-video-release/…`, built
2026-09-14 18:06 from `src/…/frame_graph_builder_program_detail.hpp` at
commit `a78a8a29e` "keep unsafe composite batches standalone"):

```sh
cd RenderingGen/renderinggen
GOAL3_RENDER_OUT_DIR=/tmp/goal3-verify CHRONON_BIN=<built chronon3d_cli> \
  go test -count=1 -run TestGoal3_FinalClipBackgroundWatermark -v ./internal/overlay/
```

**Fixed (do not re-check):**

1. **The scaled video layer is no longer absent.** All five renders now land at
   ~1.4 MB (the recorded artefacts were 75 KB for `01`/`02`); the testsrc2
   pattern is present at `(960,540)`. `a78a8a29e` (batch soundness) is the fix:
   a fused batch published only its root, so a member consumed outside the
   island handed its consumer a null framebuffer and the Composite degraded to
   a pass-through — dropping a whole layer while every node reported success.
2. **The text no longer composites at the canvas origin.** In both the normal
   and the diagnostic run the white glyph pixels occupy exactly
   `(1551,92)–(1839,139)`, which is the declared top-right box
   (`position top_right`, `margin_px 64`). The ticket's `(8,16)→(214,56)`
   signature is gone.
3. **The diagnostic flags no longer segfault.** `GOAL3_DIAG=1` (→
   `--diagnostic --diagnostic-plan` +
   `CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG=1`) now completes the whole matrix;
   the per-node trace, the per-frame composite handle dump
   (`[composite-native-diag]`) and the promotion trace are all produced. The
   frame-0 SIGSEGV was not reproducible on the current build.

**Historical second-pass evidence — superseded by the third pass above:**

The harness still fails `pixel/image_background_is_composited` (probe
`(1896,1056)` reads `rgb(0,0,0)` where the plate expects `rgb(30,90,168)`) and
the three `parity/*` means; in the non-diagnostic run
`pixel/watermark_changes_the_frame` also fails, but its 465 deviating pixels
are in the black padding, not the glyphs.

The frame graph for `01` (from the diagnostic log, frame 0) is
`image(1)`, `Video(2)`, `text(5)` → `Transform(3)` on the video →
`Composite(4)` (image ⊕ Transform) → `Composite(6)` (… ⊕ text):

```
[exec-node-diag] node_id=2 name='Video:…/source.mp4'
[exec-node-diag] node_id=3 name='Transform'        # the scaled foreground
[composite-native-diag] frame=0 result_size=1920x1080 top_size=1632x918 … top_cpu=1
```

A frame-60 pixel map of `01b` (image background + 70 % video, no text) shows the
cause exactly: the plate is correct in the left `x<288` and top `y<162` strips
and the video content sits at `(288,162)–(1632,918)` (the correct 0.7 box,
1344×756), but everything right of `x=1632` and below `y=918` is **opaque
black**, not plate. The transform's output surface is `1632×918`, i.e.
`(288,162)→(1920,1080)` = canvas minus the 0.7 centring offset, and the
untouched remainder of that surface is composited as black. Concretely:

- the Video node produces a **full-canvas** surface (a
  `ProducerSurfaceKind::Video` producer is never tight — see
  `src/render_graph/nodes/detail/producer_surface_bounds.hpp`) with the 0.7
  content drawn centred and opaque black around it;
- the `TransformNode` then applies a **pure translation of (+288,+162)** with no
  residual scale, so the content lands in the right place but the padding
  travels with it;
- the two errors cancel in *position*, which is why the layer is now visible at
  all, and leave the padding, which is the whole remaining failure.

So the fix is NOT "the layer is missing" any more. It is one of:

- make the Video producer surface **tight to the scaled content box** and drop
  the compensating translate (preferred: no full-canvas intermediate, no
  padding to declare transparent), or
- keep the full-canvas surface and composite its untouched area as
  **transparent** (the same first-write contract `draw_text_batch` already uses
  for a fresh terminal NVENC surface).

## Original symptom (FIRST PASS — kept for the record, partly falsified)

RenderingGen's Goal 3 certification harness (`renderinggen/internal/overlay/goal3_background_watermark_e2e_test.go`)
renders one composition in two lanes and pixel-compares them. On this host the
native lane fails five assertions; the frames below are frame 60 of the recorded
MP4s, so the finding is reproducible from the artefacts alone.

| Render | Size on disk | Border pixel (24,24) | Centre pixel (960,540) |
|---|---|---|---|
| `01_image_background_watermark` (image plate + 70% video + text) | **75 KB** | `rgb(245,245,245)` | `rgb(27,87,165)` = plate |
| `01b_image_background_no_watermark` (same, MINUS the text layer) | **1.39 MB** | `rgb(27,87,165)` = plate | `rgb(123,82,255)` = testsrc2 |
| `02_video_background_watermark` | 75 KB | `rgb(245,245,245)` | rust plate |
| `03_source_watermark` (full-canvas video + text, no scaling) | 1.43 MB | testsrc2 | testsrc2 |

Two facts follow directly:

1. **A 70%-scaled video layer is absent whenever a static text layer is present.**
   `01` and `02` are almost static (75 KB, flat plate everywhere except one small
   blob) while `01b` — the same composition *without* the text layer — carries the
   full test pattern (1.39 MB). The scaled layer is not mis-placed, it is not in
   the frame at all. `03`, where the video is *not* scaled (full canvas), keeps
   its content, so the collision is specifically between the **scaled video
   surface** and the **baked text surface**.
2. **The baked text is composited at the canvas origin.** Sampling the frame for
   every pixel that deviates from the plate colour (`>30` per channel) yields a
   single bounding box in the whole 1920×1080 frame:
   `(8,16) → (214,56)`. The layer's plan declares `size [320,80]` and
   `position [736,-436]` (top-right, 64 px margin), and the test's own probe box
   is `Rect(1360,40,1880,200)`. A text run at the top-left is therefore both the
   "watermark bled outside its box" failure and the reason the plate probe at
   `(24,24)` reads white (`rgb(245,245,245)` = white fill over the plate).

The same signature explains the third failure family: native-vs-software parity
deltas of **58–63/channel** on the two scaled-video cases, **15/48/8** on the
unscaled case (only the text differs there) — the size of the delta tracks
"is a scaled layer missing", not "is the render slightly off".

## What is NOT the cause (verified, so the next session does not re-check it)

- **The compiled plan is correct.** The plan recorded by the harness
  (`01_image_background_watermark_plan.json`) declares the background as
  `type: image`, `size: [1920,1080]`, `fit: cover`, `start_frame: 0`,
  `duration_frames: 120`; the video layer as `size: [1920,1080]`,
  `fit: cover`, `position: [-960,-540]`, `scale: [0.7,0.7]`; the text layer as
  `size: [320,80]`, `position: [736,-436]`. Nothing is missing from the wire
  contract, and the plan-level assertions in the harness pass.
- **The image plate itself composites correctly** — it is the plate colour across
  the whole canvas including all four corners, except inside the misplaced text
  box. The `pixel/image_background_is_composited` failure is a *symptom of the
  text placement*, not a missing plate.
- **`fit` reaches both lanes.** `source_node_native_image.cpp` passes
  `image.fit` to `compute_media_placement` (line ~96), and the reference path
  does the same through `image_renderer_draw_detail.hpp`. The
  `[native_image_diag] matrix3=[0.0,0.0,1.0,1.0]` line reports the *layer*
  transform (no scale/translate declared), which is expected for a full-canvas
  plate — it is not the fit placement.
- **The video-background path is fine**: `pixel/video_background_is_composited`
  and `pixel/video_background_is_held_to_the_last_frame` pass, so a plate
  authored at canvas geometry composites and is held past its own last frame.

## Where to look next

The collision is between two surfaces produced for the same frame. The strongest
lead is the ordering/aliasing of the two producers:

- the text node reports `[skip-diag] frame=0 node_id=5 name='text' reason=static_baked`,
  i.e. the glyphs come from a **baked static surface**, and that surface appears
  at the canvas origin — the placement (`position`, `anchor`) is not reaching the
  composite;
- the scaled video node goes through the native path
  (`ensure_empty_native_surface` + `transform_surface_affine`), which is the same
  mechanism `01b` proves works when no baked-static layer is in the plan.

A useful first experiment (engine-side, no re-encode of the plan): render `01`
with `--diagnostic --diagnostic-plan` once the flag combination no longer
crashes (below), then compare the recorded surface handles for the text and the
scaled video node. If they are the same handle, or if the text's composite is
issued last at origin, the two symptoms collapse into one bug.

## Separate, blocking finding: the diagnostic flags segfault

`GOAL3_DIAG=1` (which appends `--diagnostic --diagnostic-plan` and sets
`CHRONON3D_NATIVE_SURFACE_PROMOTION_DIAG=1`) reproduces a **segmentation fault on
frame 0 in ~3 s**:

```
[native_image_diag] path='assets/semantic/goal3-bg-image/background.png' matrix3=[0.0,0.0,1.0,1.0] anchor=[960.0,540.0] op=1.00 state_op=1.00
[skip-diag] frame=0 node_id=5 name='text' reason=static_baked
signal: segmentation fault (core dumped)
```

Reproduce with:

```sh
cd RenderingGen/renderinggen
GOAL3_DIAG=1 GOAL3_RENDER_OUT_DIR=/tmp/goal3-diag \
  go test -count=1 -run TestGoal3_FinalClipBackgroundWatermark ./internal/overlay/
```

The crash happens *after* the last diagnostic line above, so the trace the
diagnostics were added for cannot currently be produced at all. Nothing in the
same run without `GOAL3_DIAG=1` crashes.

## Verification expectations (for whoever fixes this)

The harness is the acceptance test; it is opt-in and skips without an engine, so
it costs nothing in unit CI:

```sh
cd RenderingGen/renderinggen
CHRONON_BIN=<built chronon3d_cli> \
  go test -count=1 -run TestGoal3_FinalClipBackgroundWatermark -v ./internal/overlay/
```

Green means: `parity/{image,video,source}_background_watermark`,
`pixel/image_background_is_composited` and `pixel/watermark_changes_the_frame`
all pass, with `boundary/non_canvas_video_plate_fails_closed` and
`determinism/same_plan_same_frames` still passing.
