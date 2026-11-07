# Vulkan gel text smoke — 2026-11-05

This is the first end-to-end visible render of `TextMaterial::gel()` through
the native Vulkan MTSDF text path. It is a smoke render, not a quality golden
or a stress-test pass.

## Reproduce

From the workspace root, with the `linux-video-test` CLI built:

```bash
Chronon3d/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan Chronon3d/docs/evidence/gel-text-vulkan-smoke-20261105/gel-text.plan.json \
  --assets-root "$PWD/Chronon3d" --backend vulkan --frames 0 \
  --video-sink raw --pipe-pixfmt rgba \
  -o /tmp/gel-text-vulkan-smoke.rgba
python3 Chronon3d/tools/raw_frame_to_png.py \
  --raw /tmp/gel-text-vulkan-smoke.rgba --width 1280 --height 720 \
  --frame 0 --out /tmp/gel-text-vulkan-smoke.png --require-content 0.02
```

The reviewed frame showed the cyan title on a dark background with visible
glossy bevel highlights. It also showed faceted highlight artifacts. A shader
refinement now derives a geometric roughness estimate from the loss of unit
length in the screen-space SDF gradient and broadens the specular lobe there.
The new raw capture still shows facets, so this is a partial anti-sparkle
measure, not proof of the requested optical match. No refraction, animated
light, merged glyph silhouette, or camera/depth integration is demonstrated.

Two two-second H.264 review clips are held stills made from verified raw
frames (30 fps, 60 frames); they are not animations or performance evidence:
[initial render](https://drive.google.com/file/d/1n2CLkSplYv5sdKHKhxyJOj2Kky9faqrJ/view?usp=drivesdk),
[gradient-roughness revision](https://drive.google.com/file/d/17WmyM20TtXiOqZbnlO3Lgl9knyny0Q6a/view?usp=drivesdk).

A third two-second held-still preview uses the newly configurable per-run
grazing light (`azimuth_radians=0.7`, `elevation=0.12`):
[static grazing-light preview](https://drive.google.com/file/d/1xxz2wz9ZK1rjHpZlQZvvPYiEBbNEM17x/view?usp=drivesdk).
The raw Vulkan render passed the 2% content check (3,027 white pixels;
2.91% content fraction). The image confirms that the light parameters reach
the shader, while the remaining faceted highlights are still visible. This is
not an animated 360-degree sweep.

An animated kerning stress clip exercises the existing text tracking animator
over 61 Vulkan frames at 30 fps, from +50 px to -30 px and back to +50 px:
[kerning stress preview](https://drive.google.com/file/d/1a7J5a8S0r0nBXawQqOgETF1nWzZFp_-7/view?usp=drivesdk).
The animation is deterministic and visible. At maximum overlap, however, the
glyphs remain separate surfaces with hard intersections and fragmented
highlights; this is a reproduced failure of smooth glyph merging, not a pass.
Its plan is `kerning-sweep.plan.json`.

Reproduce from the workspace root:

```bash
Chronon3d/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan Chronon3d/docs/evidence/gel-text-vulkan-smoke-20261105/kerning-sweep.plan.json \
  --assets-root "$PWD/Chronon3d" --backend vulkan --frames 0-60 \
  --fps 30 --video-sink ffmpeg -o /tmp/gel-kerning-sweep.mp4
```

## Provenance

- Plan: `gel-text.plan.json`; backdrop: `backdrop.png`.
- Raw sink: one verified 1280×720 RGBA frame (3,686,400 bytes).
- `raw_frame_to_png.py` reported 244 distinct levels, 2.91% non-background
  content, and 5,178 white pixels for the gradient-roughness revision.
- Grazing-light raw frame: `/tmp/gel-grazing.rgba`, 3,686,400 bytes; PNG:
  `/tmp/gel-grazing.png`; static still preview is linked above.
- Kerning sweep: 61 H.264 frames at 1280×720, 30 fps; 2.034 s; plan:
  `kerning-sweep.plan.json`.
- Serif zoom: 61 H.264 frames at 1280×720, 30 fps, Bodoni 72 Bold, scale
  0.05×→10×→0.05× with Z rotation 0°→15°→0°; plan:
  `subpixel-serif-zoom.plan.json`. The sweep renders, but the magnified contact
  sheet still shows faceted highlights; no temporal anti-sparkle pass is
  claimed.
- 4K/120 dynamic probe: Bodoni text with animated tracking forced a redraw on
  all 12 frames. GPU timestamp spans were 1.285–2.356 ms (median 1.798 ms);
  RGBA conversion/readback took 58–111 ms per frame. This misses the 1.5 ms
  post-processing target and does not demonstrate 120 fps end-to-end. The
  graph currently has no gel-specific bloom/dispersion pass. Raw output SHA-256:
  `eaaea69368b68ee972fe320f25201fd5bef356138ddf81663b9757653c189bdc`; raw
  payload `/tmp/gel-4k120-dynamic.rgba` (398,131,200 bytes) is temporary. Plan
  and timing sidecar are preserved as `4k120-dynamic.plan.json` and
  `4k120-dynamic.timing.json`.
- A previous draft put a full-frame color layer over the title and produced a
  misleading blank capture. This fixture uses an image backdrop at `[0, 0]`;
  the blank capture was a plan-order/placement mistake, not evidence of a
  Vulkan text failure.
