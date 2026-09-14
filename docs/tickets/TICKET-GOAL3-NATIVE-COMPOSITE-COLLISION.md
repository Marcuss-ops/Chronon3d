# TICKET-GOAL3-NATIVE-COMPOSITE-COLLISION — a baked text layer hides a scaled video layer, and lands at the canvas origin

**Status:** DIAGNOSED (read-only) — no code change in this ticket
**Created:** 2026-09-14
**Scope:** `chronon3d_cli` GPU-native lane (`required_gpu_native_full_graph`)
**Reporter:** RenderingGen audit session (read-only; RenderGen tree untouched)
**Evidence artefacts:** `refactored/ops/benchmarks/goal3-e2e-<date>/` (RenderingGen repo, sibling checkout)

## Symptom

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
