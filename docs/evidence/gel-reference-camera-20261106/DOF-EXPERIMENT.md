# DoF and 2.5D integration probe — 2026-11-06

## Result

The single-layer camera sweep does not demonstrate DoF because all text lies
on one depth plane. The current strict Vulkan/raw probe separates `calories`,
`or`, and `ingredients` into three Z planes, animates dolly and focus, and
compares matched ON/OFF plans. It now uses the Liberation Mono Bold face from
the static-reference pass (font binary and license are preserved in this
evidence directory). The background plate is centered/calibrated in the
scene at position `[215, 0, 100]` and scale `[2.8, 5.2, 1]`; camera yaw stays at
zero to avoid occluding the far word.

The sequence is 120 frames at 1094×143 and 30 fps (4 seconds). Reviewed ON
samples are `word-depth-liberation-mono-frame0.png`, `-frame30.png`,
`-frame60.png`, `-frame90.png`, and `-frame119.png`. All three words remain
visible. The sharpness transition is visible from the near word at the start
to the far word at the end. `word-depth-liberation-mono-dof-preview.mp4` is the
H.264 preview, padded to 1094×144 for 4:2:0 encoding.

Matched ON/OFF raw comparison (mean absolute RGB delta; pixels with any channel
delta >3/255):

| Frame | Mean absolute RGB | Pixels >3/255 |
| ---: | ---: | ---: |
| 0 | 2.134 | 23,594 |
| 30 | 3.351 | 35,308 |
| 60 | 2.224 | 24,351 |
| 90 | 1.376 | 13,963 |
| 119 | 1.145 | 10,775 |

SHA-256:

- ON raw: `da567373d7720f1cc1a0edd7c3c37e9caa64da0f231a8acdb58dbcc2bca07cce`
- OFF raw: `c946eb640db16ce0de39b2dcadcef4031d59c79be892abe13e182d699d263e21`
- H.264 preview: `66c2dfa32df20051713a846cbbe407428add84d0626d332c4322d73257a6c464`

## Limits and next work

This is a partial pass for camera/depth/focus behavior, not final visual
approval. The plate remains a static still rather than moving footage, and
dynamic camera parallax exposes its planar nature. The typeface and overall
spacing now follow the reference more closely, but the material body density,
highlight shape/color, and full-frame background match still need tuning. The
circular DoF kernel is implemented; iris-shaped bokeh is not. A moving footage
composite requires a video source; the provided Drive item is a 1094×143 PNG.

## Reproduction

Matched plans: `word-depth-liberation-mono-dof-on.plan.json` and
`word-depth-liberation-mono-dof-off.plan.json`. Render using
`--backend vulkan --video-sink raw --pipe-pixfmt rgba`; sampled PNGs are made
with `tools/raw_frame_to_png.py`. Static reference fit and the pinned font's
license provenance are documented in `STATIC-REFERENCE-MATCH.md`.
