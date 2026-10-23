# Documentary Snapshot Vulkan transition samples — 2026-10-21

All eleven connected recipe plans were rendered on Vulkan at frames 45, 46,
53, 54, 90, and 120. Frame 53 uses each recipe's original 90°/8-sample
shutter; the other samples use the diagnostic 1°/2-sample plans. Each of the
new frame-90 raw RGBA files is 8,294,400 bytes. These are GPU render/readback
observations, not strict-native NVENC certification.

The reviewed contact sheet is
[`gpu-transition-contact-all-v3.webp`](gpu-transition-contact-all-v3.webp).
It uses the source-fixed Reveal90 frame-90 capture from
`../gpu-fullshutter-review/reveal-90-ablation/`. The prior v1/v2 sheets
and baseline Reveal90 sample remain preserved.

Frame-90 observations:

| Recipe | Vulkan observation |
|---|---|
| archive-crane-reveal | Content visible; photo strip and title remain in transition. |
| corner-turn | Photo visible on rotated card. |
| filmstrip-handoff | Multiple photo cards visible. |
| focus-drop | Earlier capture was black; current frame-90 recheck shows the framed portrait but the title is clipped and the portrait is heavily defocused. |
| foreground-photo-pass | Earlier capture was white; current recheck shows a dark background with a large clipped title. |
| photo-stack | Large photo visible. |
| pullback-reveal | Photo and title visible at reduced scale. |
| push-through-snapshot | Photo visible under red portal tint. |
| reveal-90 | Source-fixed photo and caption visible; baseline plan was a beige card. |
| split-depth | Earlier capture failed; current frame-90 recheck shows the framed portrait and title. |
| whip-to-photo | Photo visible. |

Per-recipe `frame_090.rgba`, `.png`, and `.log` files preserve the raw sample,
projection, and command output. Recipe plans live beside those captures.
Reproduce a diagnostic sample with:

```bash
chronon3d_cli render --backend vulkan --plan RECIPE_two_sample_review.plan.json \
  --assets-root WORKSPACE --video-sink raw --pipe-pixfmt rgba \
  --start-frame 90 --end-frame 90 -o frame_090.rgba
```

## Vulkan frame-90 recheck (2026-10-21)

Re-rendered `foreground-photo-pass` frame 90 after the Vulkan temporal and
native-shape updates, still on the GPU. The result is no longer the previously
reported flat white frame: it has a dark brown background and a very large,
clipped `THE STORY OF ROME` title at the bottom. This changes the failure
signature but does not pass the visual gate; the reviewed PNG and raw RGBA are
`foreground-photo-pass/frame_090_recheck.*`, with the exact timing sidecar
beside them. Its 1°/2-sample shutter plan completed two weighted GPU samples.

The same GPU frame-90 recheck was captured for `focus-drop`. It now contains
the framed portrait instead of the previously recorded black frame. The
portrait and border are visible but heavily defocused, with the title partly
outside the top edge; this is an intermediate 1°/2-sample observation and does
not certify focus tracking or composition. See
`focus-drop/frame_090_recheck.{rgba,png,log}`.

The split-depth full-shutter frame-53 Vulkan recheck is also now complete after
fixing the cropped-framebuffer/native-surface extent mismatch in
`PerPixelDofNode`. The authored 90°/8-sample render produced a complete 8.29 MB
RGBA frame with the portrait card and title visible. Its PNG, raw frame, timing
sidecar, and invocation log are preserved under
`../gpu-fullshutter-review/split-depth/frame_053_recheck.*`. This is a
visually inspectable frame, not a full-sequence or strict-native NVENC pass.
