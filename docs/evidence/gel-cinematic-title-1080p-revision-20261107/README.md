# GEL cinematic title — 1080p anamorphic wing revision (2026-11-07)

## Result

The edge-tracing streak is superseded. Test C now uses a **column-collapsed
anamorphic wing**: the title's rows are averaged into one column profile, that
profile is stretched horizontally, and it is faded by a narrow vertical
Gaussian. This removes both the old glyph-edge comb and the rectangular band
produced by a full 2D blur clipped to the text layer.

The new final is **`GEL_Cinematic_Title_1080p_Wing.png`** — 1920×1080 RGBA,
1,130,685 bytes, SHA-256
`61834d2f39621353566d35d3fd98cbb9af3ad67d5fc8d561d34f6e465a1b5879`.

Test C is **`C_gel_wing_final.png`** — 1,053,174 bytes, SHA-256
`8c78577acfef919d15ef7d2f786bfd7d4464679d058e96df2bf9b334b4e67364`.

The no-flare candidate remains **`GEL_Cinematic_Title_1080p_Corrected.png`** —
1,122,803 bytes, SHA-256
`382c36831ac84fe700ed7ceaa950e9fb4329ec2a95348d5ad224ed7294f5d3f4`.
It is byte-identical to `D_no_flare.png`.

`GEL-1080P-VISUAL` remains **pending human review**. A new 10-second
1920×1080/30 fps clip was encoded from `revision_wing.plan.json` with native
NVENC and uploaded with the PNG; the old video and poster were not touched.

## Drive delivery

Both files were uploaded to Drive folder **`Tutti`**
([folder](https://drive.google.com/drive/folders/1ck40GwTSzctOZjzNQzkY0_JvKD11_U6g);
id `1ck40GwTSzctOZjzNQzkY0_JvKD11_U6g`) and verified by `drive-upload` against
the local SHA-256 and byte count.

- final PNG: <https://drive.google.com/file/d/1LC7V2i-CEzZOGPQujxzCyBrTKE7swKeR/view?usp=drivesdk>
- 10-second clip: <https://drive.google.com/file/d/1sTLdqZ9BExzBYhmTR-Zv9s6z2-ILrJ9G/view?usp=drivesdk>
- local upload copies and logs:
  `RenderingGen/UploadDrive/gel_cinematic_title_wing_20261107/`

## Test matrix

All test-matrix renders are one frame, raw RGBA sink, Vulkan,
`--gpu-hot-path-mode require_gpu_native`, device 0. No fallback line is present
in the final logs. The separate 10-second clip is documented below.

| Test | Content | Result | Evidence |
| --- | --- | --- | --- |
| A | background only | **PASS** — no seam at y=415/666 | `A_background.png` |
| B | GEL faces + backings, no optics | **PASS** — solid cyan face, no ghost glyphs | `B_gel_no_effects.png` |
| C | only `light.lens_flare` per word | **PASS (technical)** — thin wing, no glyph comb or clipped rectangle | `C_gel_wing_final.png` |
| D | full composite with wing | **PASS (technical)** — inherits C; wing sits at y=514..566 | `D_full_wing_final.png` |
| D | full composite, flare off | **PASS** — byte-identical to the corrected no-flare PNG | `D_no_flare.png` |

The previous fixed candidate (`C_gel_flare_fixed.png` / `D_full_fixed.png`) is
retained as history; it still showed a faint glyph-edge comb. The old failing
`C_gel_flare.png` and `D_full.png` are also retained.

## Measurements

- C final: `render_ms=510.19`; D final: `render_ms=732.14`.
- C final vs B: 30,764 pixels differ by more than 2, bbox
  `(1, 520)–(1902, 558)`, max 64, mean 0.222.
- D final vs `D_no_flare`: 35,362 pixels differ by more than 2, bbox
  `(0, 514)–(1902, 566)`, max 77, mean 0.243.
- Final vs `A_background` at seam rows 413–415 and 664–666: mean absolute
  delta `0.0` on every row.
- Pack contract: **14 checks, 0 failures**.

## 10-second delivery clip

`GEL_Cinematic_Title_1080p_Wing_1920x1080_10s.mp4` was rendered from
`revision_wing.plan.json` over frames 0–299 with Vulkan hot-path enforcement
and native NVENC (`h264_nvenc`). The encoder log reports 300 frames accepted and
300 packets muxed; no software fallback is present.

- ffprobe: H.264 High, 1920×1080, 30/1 fps, 300 frames, duration `10.000000` s.
- size: 2,382,860 bytes; SHA-256
  `df232c5ac15fef3306d175b73ec56c2589ce2ec8bade66b247971b5eb900f533`.
- render wall time: 135,431.93 ms; `render_ms=133,661.30`.

## What changed

- **Column-collapsed wing.** Nine vertical samples across the text band are
  averaged into a single horizontal profile. A Gaussian with
  `sigma_x = min(1.8 × radius, 96 px)` stretches it; a narrow profile with
  `sigma_y = min(0.04 × radius, 8 px)` places it around the optical center.
- **Highlight mask.** Threshold `0.55`, with the existing `0.06` smoothstep
  knee. This includes the cyan text body while keeping the dark plate out.
- **Pack parameters.** `enableLensFlare=true`, `radius=0.18`,
  `intensity=1.1`, `halo=0`, `ghost=0`, `streak=1`.
- **Removed artifacts.** The previous horizontal-row kernel, 5-tap vertical
  prefilter and broad 2D Gaussian are gone. The rejected broad candidates were
  deleted; only the final artifacts remain.
- **Software parity.** The software processor implements the same column
  collapse, sampling count, sigmas and thresholds. Its local mask framebuffer
  still avoids the cross-layer scratch race.

## Composition and provenance

- **One plate**, `plate_full_frame.png` (1920×1080, 607,442 bytes).
- **3 extruded backings** + **3 opaque GEL faces**, Liberation Mono Bold, at
  the certified reference positions.
- **7 layers**: 1 plate image + 3 backings + 3 faces. Each face carries drop
  shadow → chromatic dispersion → bloom → `light.lens_flare`.
- The final plan is `revision_wing.plan.json`; Test C is
  `C_gel_wing_final.plan.json`. The earlier no-flare plan remains
  `revision.plan.json`.

## Artifacts

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `plate_full_frame.png` | 607,442 | `8688c4a9071fcd26ad63741fc357801ab9f38f8b1ec1da62fcc84eb724734167` |
| `revision.plan.json` (no flare) | 748,211 | `e34d593adff6684580bb52c9df1374b8844b2dca4fedcb9750459e84f7973b86` |
| `revision_flare.plan.json` (previous) | 750,197 | `38d3345b95fe95ef366c8e9d4411784345b10426ca0fa493d610ec67409dc48b` |
| `revision_wing.plan.json` | 750,185 | `497c4f877c7bf902ff8b19ebaece6e56bbcbdb1ec7694675bf38e183b1169359` |
| `C_gel_wing_final.plan.json` | 255,203 | `722cadd2a23f5ae89dec6504c9ecc10595492836f2d62f4dcc6bf4e8cd7052ec` |
| `C_gel_wing_final.png` | 1,053,174 | `8c78577acfef919d15ef7d2f786bfd7d4464679d058e96df2bf9b334b4e67364` |
| `D_full_wing_final.png` | 1,130,685 | `61834d2f39621353566d35d3fd98cbb9af3ad67d5fc8d561d34f6e465a1b5879` |
| `GEL_Cinematic_Title_1080p_Wing.png` | 1,130,685 | `61834d2f39621353566d35d3fd98cbb9af3ad67d5fc8d561d34f6e465a1b5879` |
| `GEL_Cinematic_Title_1080p_Wing_1920x1080_10s.mp4` | 2,382,860 | `df232c5ac15fef3306d175b73ec56c2589ce2ec8bade66b247971b5eb900f533` |
| `D_no_flare.png` | 1,122,803 | `382c36831ac84fe700ed7ceaa950e9fb4329ec2a95348d5ad224ed7294f5d3f4` |
| `GEL_Cinematic_Title_1080p_Corrected.png` | 1,122,803 | `382c36831ac84fe700ed7ceaa950e9fb4329ec2a95348d5ad224ed7294f5d3f4` |
| `A_background.png` | 1,063,481 | `6fd7a4e91f7c8de6feaa23486259b4ca6afccc94e980fa5ebf81d7ed764f148f` |
| `B_gel_no_effects.png` | 1,050,722 | `e0e8ca2444031faf7fa781b2e870ea6aa1f87c5ad5eec185eae3d5334d742939` |
| `C_gel_flare_fixed.png` (previous) | 1,075,233 | `e0b0d1bfdce36a5df9ea4c9483a56f878933cb3e4fcff8fa2549afd697d23521` |
| `D_full_fixed.png` (previous) | 1,143,951 | `1f090b3cc8fa2f8ce994a94a3f28b92b854b5cfd8a37e64238c9a17072359b5a` |
| `GEL_Cinematic_Title_1080p_Flare.png` (previous) | 1,143,951 | `1f090b3cc8fa2f8ce994a94a3f28b92b854b5cfd8a37e64238c9a17072359b5a` |
| `C_gel_flare.png` (old FAIL) | 1,102,220 | `52e0f0ef04bc21e9506917c9974a35ab19075b0fd1381fc253bace36270edc0a` |
| `D_full.png` (old FAIL) | 1,173,664 | `46eefc210aec1fa6a5cb7045b695a5aa7eac687747b64a538c74eb7b86c5d26a` |
| `tests_A-D_wing_contact.png` | 215,238 | `833efe3169adba30e469a04abbf4bcf10cd648365050099bf0eb6cbca8963a38` |

The old video and poster remain historical evidence:

- `../gel-cinematic-title-1080p/frame0.png` —
  `6b07f4f62c72fcdfe28ae87fe7e88871797adfcde3becfc11667536973536f46`
- `../gel-cinematic-title-1080p/GEL_Cinematic_Title_Closure_1920x1080_10s.mp4` —
  `49897674c03f055c1917c39ae81bae6a3dd69913c46175f751f58d92343e8075`

Neither was overwritten or re-rendered. The new 10-second clip is a separate
file.

## Reproduction

Build the release CLI (manifest install is disabled to reuse the installed
dependencies):

```sh
cd Chronon3d
cmake --preset linux-video-release -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build/chronon/linux-video-release --target chronon3d_cli -j16
```

Build and run the pack contract, then emit the final wing plan:

```sh
cd ChrononTemplate
ninja -C build/dev-fast \
  chronontemplate_gel_cinematic_title_test \
  chronontemplate_emit_gel_cinematic_title -j4
./build/dev-fast/chronontemplate_gel_cinematic_title_test
cd ../Chronon3d
../ChrononTemplate/build/dev-fast/chronontemplate_emit_gel_cinematic_title \
  docs/evidence/gel-cinematic-title-1080p-revision-20261107/revision_wing.plan.json \
  docs/evidence/gel-cinematic-title-1080p-revision-20261107/plate_full_frame.png \
  docs/evidence/gel-reference-camera-20261106/LiberationMono-Bold.ttf
```

Render C and D-full on the GPU, then convert the raw sinks:

```sh
CLI=build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
EV=docs/evidence/gel-cinematic-title-1080p-revision-20261107
$CLI render --plan "$EV/C_gel_wing_final.plan.json" \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 0 --video-sink raw --pipe-pixfmt rgba \
  --output "$EV/C_gel_wing_final.rgba"
$CLI render --plan "$EV/revision_wing.plan.json" \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 0 --video-sink raw --pipe-pixfmt rgba \
  --output "$EV/D_full_wing_final.rgba"
python3 tools/raw_frame_to_png.py --raw "$EV/C_gel_wing_final.rgba" \
  --width 1920 --height 1080 --out "$EV/C_gel_wing_final.png"
python3 tools/raw_frame_to_png.py --raw "$EV/D_full_wing_final.rgba" \
  --width 1920 --height 1080 --out "$EV/GEL_Cinematic_Title_1080p_Wing.png"

# 10-second review clip, native NVENC
$CLI render --plan "$EV/revision_wing.plan.json" \
  --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 299 --fps 30 \
  --hardware nvenc --encoder-backend native --codec h264 \
  --output "$EV/GEL_Cinematic_Title_1080p_Wing_1920x1080_10s.mp4"
```

## Limitations

- **Human visual approval is still pending.** The wing is deliberately subtle
  (about 52 px tall in the final composite) and may need a final taste pass.
- **The clip is a review encode, not a final master.** It is 1080p H.264 at
  30 fps, 10.000 s; 4K/120 was not run. The clip passed ffprobe and encoder-log
  checks, but no frame-by-frame visual inspection was performed.
- **The Drive upload is not sign-off.** The files are in folder `Tutti` for
  review; human approval remains pending.
- **The dedicated `test_stylize` case was not executed in this release build**
  (`CHRONON3D_BUILD_TESTS=OFF`). The pack contract test did run and pass.
- **Software is validation-only.** The software processor implements the same
  wing, but the latest C frame was not rendered through the software backend.
- **The previous Drive artifact is unchanged.** The new upload is a separate
  PNG and a separate 10-second clip; the historical 10-second video was not
  overwritten.
