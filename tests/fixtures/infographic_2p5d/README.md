# 2.5D infographic fixtures

Executable probes for the v3 render-plan contract, in the spirit of
`text_3d_stress`: every layer is built from a primitive the contract exposes, and
this file records what a render actually shows.  Every number below comes from
decoding the rendered frames with `measure.py` — not from reading the schema.

## Layout

| File | Contents |
|---|---|
| `plan_lib.py` | canvas/camera constants, the authoring helpers, the plan envelope |
| `scene_probe.py` | `00` — one layer per primitive, the capability probe |
| `scene_camera_regressions.py` | `10`–`13` — the minimal repro of the closed 2.5D camera defect |
| `scene_infographic.py` | `20` — the full 24-layer scene |
| `generate.py` | registry + writer (the only entry point) |
| `measure.py` | decode a frame, report ink / bbox / colour bands / per-element regions |
| `determinism.py` | prove the pre-encode lane is bit-exact and the mp4 lane keeps its structural contract |

## Reproduce

```bash
cd Chronon3d
CLI=build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli

python3 tests/fixtures/infographic_2p5d/generate.py

for plan in tests/fixtures/infographic_2p5d/plans/*.plan.json; do
  name=$(basename "$plan" .plan.json)
  "$CLI" render --plan "$plan" -o "/tmp/$name.mp4" \
    --assets-root "$PWD" --backend software
done

python3 tests/fixtures/infographic_2p5d/measure.py --regions /tmp/20_scene.mp4:42

# Reproducibility: PASS with one render worker, FAIL on the default scheduler
# (see "Render determinism" below).
python3 tests/fixtures/infographic_2p5d/determinism.py 3 0-19
python3 tests/fixtures/infographic_2p5d/determinism.py 3 0-19 --parallel
```

Always render with `--backend software`: the Vulkan backend aborts on
`apply_effect_stack` with *"RenderSurface execution is not wired yet"*.

## Conventions this fixture pins down

Measured, not assumed.  Getting these wrong makes a layer silently vanish or
land somewhere plausible-looking, which is why each row carries its evidence.

| Rule | Evidence |
|---|---|
| A **2D layer's `position`** is its centre in canvas pixels, origin top-left | a 200×200 `rect` at `[480, 270]` on 960×540 renders exactly 200×200 there; the `10`/`11` ribbon authored at `[480, 420]` measures `(218,425,739,486)` |
| An **`enable_3d` layer's `position`** is in the **camera basis**: origin at the canvas centre, **+y up** | the `11`/`12` disc authored `(280, 190)` measures `(618,61,893,99)` → canvas centre (760, 80). Pinned in-tree by `tests/render_graph/pipeline/test_pipeline_robustness_coordinate.cpp` (*"Camera2_5D uses screen-Y-down coordinates: authored y=50 maps to canvas y=540-50=490"*) |
| `position` **keyframes** are absolute in the *same* space as the layer's static position | the `00` disc animates to `[-240, 60]` and measures centred at `(241, 210)` = `(240, 270-60)` |
| A camera whose `focal_px == distance` leaves the z=0 plane at **1:1** | `11` versus `10`: the ribbon's white bbox is bit-identical. `plan_lib.one_to_one_camera()` derives the fov from the canvas height instead of hard-coding it |
| There is **no `ShapeType::Ellipse`**: a plan `ellipse` becomes a `Circle` of diameter `min(size)` | size `(240,80)` and `(80,240)` both render 80×80; `(300,104)` renders 104×104. A true ellipse needs `enable_3d: true` **plus** non-uniform `scale` — the `20` disc is `size (300,300)` + `scale [1, 0.35, 1]` and measures 300×56 |
| Non-uniform `scale` **requires `enable_3d: true`** | a plan with `scale` and no `enable_3d` fails preparation: *"3D transform data requires enable_3d=true"* |
| Gradient geometry is **normalised to the shape's box in [0,1]**, not canvas pixels | `tests/c_abi/test_render_plan_shape_helpers.hpp` authors `start [0,0]` → `end [1,0]`; pixel-sized values clamp the whole shape to one stop |
| A shape needs `size` **and** explicit `start_frame` / `duration_frames` to draw | omitting them left layers blank |
| `path` commands are authored in **node-local** space, centred on the layer position | the `(±300, 70)` hero bezier at `position [480, 420]` measures `(218,425,739,486)` |
| `layers` paint in **array order**, later on top | the `20` scene relies on it for background → texture → content → badges |

## Probe results (`00_vocabulary_probe`)

Frame 59 of 60, 960×540, software backend, 80 415 ink pixels (lum > 60).  Each
row was measured inside that layer's own region, with the render pinned to one
worker so the frames are reproducible (see "Render determinism"):

| Layer | Primitive | Status |
|---|---|---|
| `bg_gradient` | `rect` + linear gradient + `background_look` film grade | PASS — fills the canvas, gradient visible |
| `tilted_ring` | `ellipse` + `enable_3d` + `scale` + `rotation [58,0,0]` | PASS — a true tilted disc, 340×56 |
| `flow_ribbon` | `path` + `stroke` + animated `stroke_dash_offset` | PASS — write-on: ink inside the ribbon box `(180,340,780,500)` grows 9 413 px (frame 12) → 12 416 px (frame 30) → 12 998 px (frame 59) |
| `ribbon_head` | `ellipse` + `follow_path` | PASS — the only white ink at the path start before the stroke is drawn |
| `social_dots` | `dot_grid` | PASS — 9 164 px in the authored box |
| `badge_*` | `rounded_rect` + `grid_layout` | PASS — placed by the layout group |
| `counter` | `text` resolving `${clicks}` | PASS — `variables` resolves into the glyph run |

## Defects found while building this fixture

| Defect | Root cause | Regression test |
|---|---|---|
| An animated dashed stroke could not be rendered at all: `unsupported primitive animation property: stroke_dash_offset` at scene build | `apply_animation_tracks` (`render_plan_compiler_animation.cpp`) had no branch for the per-frame paint-track family, although the decoder validates it, the shape compiler samples it and the v3 schema lists it. The hole also covered `stroke_width`, `stroke_color`, `fill_color` and the `shadow_<0..2>_{opacity,blur,offset_x,offset_y,color}` family | `tests/c_abi/test_render_plan_decoder.cpp`: *per-frame paint tracks compile into a scene instead of aborting* |
| CLI validation rejected legal v3 plans with *"instance not found in required enum"* | `v2_projection` erased only `fill_color`/`skew`, so the frozen v2 enum was checked against v3-only tracks. Now a `v2_projection` keeps every property the v2 enum can express (`is_v2_layer_track_property`) | `tests/c_abi/test_render_plan_validator.cpp`: *render_plan V3 accepts the per-frame paint track family* |
| A `path` shape rendered as a ~20 px blob at the node origin — so no bezier ribbon, no write-on line and no isometric face could be drawn | `renderer::compute_world_bbox` (`shape_rasterizer.cpp`) had no `ShapeType::Path` case: it returned `{0,0}`, the bbox collapsed to a 20×20 square at the node origin, and the graph executor used that as the clip rectangle. `stroke_width_for_shape` also lacked the `Path` case | `tests/renderer/helpers/test_shape_path_world_bbox.cpp` (reverting the fix fails 9 assertions) |
| **A layer with `enable_3d` made every 2D shape in the same composition disappear** | `evaluate_source_payload_placement` handed the pixel-space layer matrix to `project_to_camera_space`, which composes its own `canvas_center * ssaa * proj` prefix for the centre-relative y-up layer basis. The centre was applied twice and y mirrored: the ribbon authored at canvas `[480, 420]` on 960×540 projected to `(757.9, 26.8)` and left the frame. Fixed by `detail::camera_input_matrix()` in `graph_builder_coordinates.hpp`, the exact inverse of that prefix plus the pixel→camera y-basis change | `tests/render_graph/pipeline/test_pipeline_robustness_placement.cpp`: *evaluate_source_payload_placement keeps 2D layers still under a head-on 2.5D camera* (dropping the fix fails 3–5 assertions) |

## Camera regression family

The minimal, rerunnable form of the `enable_3d` defect.  All four share one
ribbon, so the numbers are directly comparable (frame 42, 960×540):

| Plan | White ribbon bbox | Verdict |
|---|---|---|
| `10_regression_2d_reference` (ribbon only) | `(218,425,739,486)` | reference |
| `11_regression_2d_under_3d` (ribbon + `enable_3d` disc + 1:1 camera) | `(218,425,739,486)` | **identity** — a camera and a 3D layer may not move the 2D layout |
| `12_regression_2d_under_implicit` (ribbon + `enable_3d` disc, compiler-derived camera) | `(218,425,739,486)` | identity — the implicit camera is 1:1 too |
| `13_regression_2d_plus_color_3d` (ribbon + `enable_3d` `color` layer + 1:1 camera) | `(218,425,739,486)` | identity |

Before the fix all four of `11`–`13` rendered an empty frame.

Note the split this family encodes: the *2D* layers are unaffected by the camera,
while the *3D* layers inside the same composition are placed by it — the disc in
`11`/`12` measures `(618,61,893,99)`, i.e. exactly where its camera-space
`(280, 190)` says canvas centre + `(280, −190)` is.

## The scene (`20_scene`)

24 layers, 960×540, 60 frames, explicit 1:1 camera so the 2D layout stays 1:1
while the disc gets perspective.  Content: gradient background with a film-grade
look, dot-grid texture, title + subtitle, a tilted 3D disc, a four-stage funnel
of tapered closed paths, three `${variable}` counters, a six-point step-line
chart with markers, the write-on hero ribbon with a `follow_path` head, and three
`grid_layout` badges.

Frame 42 measures 130 396 ink pixels (lum > 60): white 7 938, warm 18 526,
cool 20 201, teal 10 664.  Every element group draws inside its authored box —
reproduce with `measure.py --regions`, whose `REGIONS` table holds the boxes
(generous on purpose, so neighbours may overlap at the edges):

| Region | Ink |
|---|---|
| title + subtitle | 28 816 |
| tilted 3D disc | 11 405 |
| funnel stages | 14 451 |
| counters | 14 745 |
| step chart | 9 882 |
| ribbon | 25 289 |
| badges | 9 565 |

## Test status

`chronon3d_c_abi_tests` 212/212, `chronon3d_render_graph_tests` 416/416,
`chronon3d_renderer_core_tests` 176/176, `tools/check_test_hygiene.sh` 4/4.

The stroke-gradient colour failures this file used to report as unrelated
pre-existing are closed.  `resolve_stroke_gradient_color` samples through the
renderer's spatial ordered dither (`graphics::dither_gradient_sample`, added
2026-09-27), so the undithered expectations the test still carried could no
longer hold: every observed value matched
`analytic + ((token + 0.5)/64 - 0.5)/255` for its dither cell bit-for-bit, with
alpha untouched.  The expectations now compare within the renderer's own
±0.5/255 amplitude, and a new case pins the per-pixel dither against an
isolated single-stop gradient.

## Render determinism (open defect)

Two lanes, two contracts, and only one of them holds today.

**Pre-encode frames** (`--video-sink raw`) are the renderer's own output — the
lane a reproducibility claim lives in — and on the default parallel scheduler
they are **not** bit-exact for a multi-layer scene: three runs of `20_scene`
produce three different byte streams. `CHRONON3D_SCHEDULER_WORKERS=1` restores
bit-exactness (3/3 identical); `--tile-size 0|128|256` all still diverge, so
this is not the tile-dirty-rect path.

**The mp4 lane** is lossy, so its contract is structural (frame count,
timestamps), which it keeps.  Its instability is a *symptom*, not a second
defect: standalone ffmpeg/libx264 encoding the same pre-encode bytes is 6/6
byte-identical (including when the frames are fed with deliberate pacing), the
x264 options written into both a stable and an unstable file are identical, and
the mp4 lane comes out byte-identical (3/3) once the render is pinned to one
worker.

Where the divergence sits: in the pre-encode frames it is confined to the
animated dashed-path ribbon.  Frames 0-10 are identical; frame 20 differs in
1 283 px inside `(190,430,792,494)` and frame 29 in 1 922 px inside
`(177,415,641,482)` — the ribbon's own box — across all three colour channels,
up to 64 levels.  A single-layer ribbon plan did not reproduce it in ~22 runs,
so the race needs the full pipeline's parallel work to interleave.

`determinism.py` is the check (its exit status is the verdict).  Until the race
is fixed, renders that must be reproducible pin `CHRONON3D_SCHEDULER_WORKERS=1`
— every number in this file was measured that way.
