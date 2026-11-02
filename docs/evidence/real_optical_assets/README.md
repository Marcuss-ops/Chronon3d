# Real optical asset fixtures (2026-11-02)

Six 1920×1080, 150-frame RenderPlan V3 fixtures produced by
`ChrononMotion3D/tools/plan_optical_studio.cpp`, plus the image plates they
composite.

| Mode | Plan | Intent |
|---|---|---|
| `dof` | `real_optical_dof_1080p.plan.json` | Depth of field with a racking focus distance |
| `sharp` | `real_optical_sharp_1080p.plan.json` | Sharp control for the DoF pair |
| `shutter` | `real_optical_shutter_1080p.plan.json` | Full-scene temporal motion blur, 9 camera samples per frame |
| `dolly_in` | `real_optical_dolly_in_1080p.plan.json` | Forward dolly with a focus pull to the near plate |
| `orbit` | `real_optical_orbit_1080p.plan.json` | Lateral orbit with roll and a moving focus plane |
| `vertigo` | `real_optical_vertigo_1080p.plan.json` | Dolly-out with an opposing FOV change |

## Regenerating

The asset prefix is the second tool argument and must stay `"."` so the
references resolve relative to the mounted asset root:

```sh
cd ChrononMotion3D
D=../Chronon3d/docs/evidence/real_optical_assets
./build/release-fast/chrononmotion_plan_optical_studio "$D/real_optical_${m}_1080p.plan.json" "." 1920 "$m" offline
```

An earlier run used a non-empty prefix. That baked
`assets.docs/evidence/real_optical_assets/Inter-Bold.ttf` into every plan, so
`chronon3d_cli validate` failed with `logical asset does not exist under the
mounted root` on all six. Regenerated with `"."` on 2026-11-02.

## Validating

```sh
Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli \
  validate --plan "$D/real_optical_${m}_1080p.plan.json" --assets-root "$D"
```

All six return `status=PASS, valid=true, width=1920`. Plan digests after the
2026-11-02 regeneration (SHA-256, first 16 hex):

| Mode | Plan digest |
|---|---|
| dof | `0db36badbde20148` |
| sharp | `c1e506c5a152a6a9` |
| shutter | `75c15ff2045cfcef` |
| dolly_in | `95d796083ab241b8` |
| orbit | `ae96c7e97b150203` |
| vertigo | `f6ce21cc1b151f2b` |

## Rendering

The `mp4` sink cannot be used directly in this build: it requests `libx264`
from the native FFmpeg path, which is disabled here
(`avcodec_find_encoder_by_name('libx264') failed`, `EncoderFailed`). The working
route is raw sink → system ffmpeg:

```sh
"$CLI" render --plan "$D/real_optical_${m}_1080p.plan.json" --assets-root "$D" \
  --backend software --start-frame 0 --end-frame 149 --video-sink raw --output /tmp/${m}.rgba
ffmpeg -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i /tmp/${m}.rgba \
  -c:v libx264 -pix_fmt yuv420p -crf 16 -preset medium "$D/real_optical_${m}_1080p.mp4"
```

All renders on 2026-11-02: `--backend software`, 150 frames, exit 0, decode
1920×1080 `yuv420p`, 150 frames read back by `ffprobe -count_frames`.

| Mode | MP4 bytes | Notes |
|---|---:|---|
| dof | 1,427,362 | DoF pair with `sharp` |
| sharp | 2,584,267 | sharp control |
| dolly_in | 2,184,873 | new dolly mode |
| orbit | 2,094,337 | new orbit mode |
| vertigo | 2,236,333 | new dolly-zoom mode |
| shutter | 1,448,157 | 9 samples per frame; the full 150-frame render takes ~40 min |

The DoF pair is not vacuous: frame 75 of `dof` versus `sharp` gives RGB
MAE 5.011, p99 38, max 95 on the 0–255 scale.

The shutter pass is neither a copy of `sharp` nor static: grey MAE versus
`sharp` is 23.62 at frame 0, 4.85 at frame 74 and 5.23 at frame 149, and
`shutter` frame 0 versus frame 149 gives MAE 25.89. The blur therefore tracks
the camera motion instead of being applied as a constant filter.

`real_optical_breakdown_1080p.mp4` and `real_optical_cards_breakdown_1080p.mp4`
were produced by the `breakdown` and `cards_breakdown` modes (180 frames) and
have no plan committed alongside them; regenerate with the same command and
the corresponding mode if they are needed as evidence.

## Repo weight policy

These fixtures carry 25 MiB of PNG plates and MP4 renders plus ~4 MiB of
per-frame timing sidecars. Generated media stays **out of git** until the
external asset pack and fetch script described in
[`../repo-weight-audit-20261031.md`](../repo-weight-audit-20261031.md) exist;
[`.gitignore`](.gitignore) excludes it here. Plans, render logs, the font and
this README are the committable part.

`near.png`, `middle.png`, `far.png` and `background.png` are the composited
plates; `Inter-Bold.ttf` is the title font and is referenced by every plan.
