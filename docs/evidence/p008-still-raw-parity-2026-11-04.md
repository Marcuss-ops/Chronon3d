# P0-08 still/raw pixel comparison — 2026-11-04

This is focused local evidence for the raw-to-PNG alpha/color conversion, not
release certification. It uses the existing 128x96 image+mesh+text RenderPlan
fixture rendered with the locally built CLI (`/tmp/chronon-p0-08.plan.json`).

The raw sink stores sRGB-encoded premultiplied-linear RGB plus linear alpha.
The PNG writer stores straight-alpha sRGB. `tools/raw_frame_to_png.py` now
decodes sRGB, unpremultiplies in linear light, and re-encodes RGB; the raw
profile is still computed over the original raw bytes.

Commands:

```sh
tools/raw_frame_to_png.py --self-test
tools/raw_frame_to_png.py --raw /tmp/chronon-p0-08.raw --width 128 --height 96 \
  --frame 0 --out /tmp/chronon-p0-08-raw-converted.png --require-content 0.001
ffmpeg -v error -i /tmp/chronon-p0-08-still.png -f rawvideo -pix_fmt rgba /tmp/p0-08-still.rgba -y
ffmpeg -v error -i /tmp/chronon-p0-08-raw-converted.png -f rawvideo -pix_fmt rgba /tmp/p0-08-converted.rgba -y
```

Result: 12/12,288 pixels differ; maximum channel delta 1; mean absolute channel
error 0.000732421875; p99 channel error 0. This is a fixture result only.
Repeatable same-commit complete scene coverage and confirmation that the still
lane composes all required layer types remain release gates.
