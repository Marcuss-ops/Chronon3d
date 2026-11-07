# GEL cinematic title — 1080p ChrononTemplate closure (2026-11-07)

> **Superseded — do not use this closure as approved.** A later 1080p review on
> 2026-11-07 found a hard seam at y=415/666 from the 1920×251 strip over the
> blurred backdrop, plus repeated glyph fragments from the per-word anamorphic
> flare. `GEL-1080P-VISUAL` is **FAIL** for this poster. The corrected still and
> the A–D evidence are in
> [`../gel-cinematic-title-1080p-revision-20261107/`](../gel-cinematic-title-1080p-revision-20261107/README.md).
> The video and poster hashes below remain historical artifacts and were not
> overwritten or re-rendered.

## Result

**Historical PASS — superseded (see banner).** A 1920×1080, 30 fps, 300-frame (10.000 s) H.264 video of
“calories or ingredients”, authored by the ChrononTemplate
`gel::GelCinematicTitlePack` preset and rendered by the existing Chronon3D
RenderPlan consumer.

The composition uses:

- a sharp 1920×251 reference strip pinned at `y=0`, over a blurred
  full-frame extension of the same plate;
- three real `extruded_text` 3D backing words at depth `8`, with the
  `generate.fill` pass;
- three opaque 3D GEL faces (`gel_transmission: false`) with the selective
  drop shadow, chromatic dispersion, tight glow, thresholded bloom and
  horizontal anamorphic streak;
- a perspective camera calibrated so one world unit is one output pixel at
  `z=0`: position `[0, 5, -1080]`, FOV `53.1301°`, rotation `[0, 0, 0]`.
  Both the backing and the face use that camera, so the slow 1.035× push stays
  solid between the extrusion and the GEL surface.

No Chronon3D renderer or shader change was made for this closure. The changes
are in ChrononTemplate and the ChrononMotion3D RenderPlan bridge.

## Artifacts

The artifacts below are the current closure. `gel_cinematic_title_1080p.mp4` and
its `.timing.json` are an earlier superseded attempt and are not part of this
certification.

| file | bytes | SHA-256 |
|---|---:|---|
| `gel.plan.json` | 750834 | `ec64967bb5583e7f7f08e087f0a2ee798a8c4cee949db57aa51461dace14337c` |
| `GEL_Cinematic_Title_Closure_1920x1080_10s.mp4` | 2482096 | `49897674c03f055c1917c39ae81bae6a3dd69913c46175f751f58d92343e8075` |
| `frame0.png` | 1100323 | `6b07f4f62c72fcdfe28ae87fe7e88871797adfcde3becfc11667536973536f46` |
| `frame299.png` | 1115248 | `1b6789d61e1137bf1dac67c070d1161a2b5c6a2a33f92ea91570223d47ccd8f0` |
| `video-frames-contact.png` | 650759 | `d02c3809c2ad6a17f4eec33452721853ad6c3151eccbacad24662039e51d7b7a` |

`video-frames-contact.png` contains frames at 0 s, 5 s and 9.9 s extracted from
the encoded MP4, not from the raw render buffers.

## Verified video properties

```text
codec_name=h264
width=1920
height=1080
r_frame_rate=30/1
nb_frames=300
duration=10.000000
size=2482096
```

The encoder log records a successful write and
`sha256=49897674c03f055c1917c39ae81bae6a3dd69913c46175f751f58d92343e8075`.
The independent `sha256sum` of the written file matches.

## Reproduction

```sh
cd ChrononTemplate
cmake --build build/dev-fast \
  --target chronontemplate_gel_cinematic_title_test \
           chronontemplate_emit_gel_cinematic_title -j12
./build/dev-fast/chronontemplate_gel_cinematic_title_test
./build/dev-fast/chronontemplate_emit_gel_cinematic_title \
  ../Chronon3d/docs/evidence/gel-cinematic-title-1080p/gel.plan.json \
  docs/evidence/gel-text-stress-20261105/gel-reference-plate-20261106.png \
  docs/evidence/gel-reference-camera-20261106/LiberationMono-Bold.ttf \
  output/gel_cinematic_title_1080p.mp4

cd ../Chronon3d
build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
  --plan docs/evidence/gel-cinematic-title-1080p/gel.plan.json \
  --assets-root "$PWD" \
  --backend vulkan \
  --gpu-hot-path-mode require_gpu_native \
  --start-frame 0 --end-frame 299 --fps 30 \
  --output docs/evidence/gel-cinematic-title-1080p/GEL_Cinematic_Title_Closure_1920x1080_10s.mp4
```

`--fps` is a single integer in this CLI; `30:1` is rejected. The 30 fps plan
already carries `fps_num: 30`, `fps_den: 1`, `duration_frames: 300`.

Verification commands used:

```sh
ffprobe -v error \
  -show_entries format=duration,size:stream=codec_name,width,height,r_frame_rate,nb_frames \
  -of json \
  docs/evidence/gel-cinematic-title-1080p/GEL_Cinematic_Title_Closure_1920x1080_10s.mp4
sha256sum \
  docs/evidence/gel-cinematic-title-1080p/gel.plan.json \
  docs/evidence/gel-cinematic-title-1080p/GEL_Cinematic_Title_Closure_1920x1080_10s.mp4 \
  docs/evidence/gel-cinematic-title-1080p/frame0.png \
  docs/evidence/gel-cinematic-title-1080p/frame299.png
```

## Drive delivery

Uploaded with `RenderingGen/bin/drive-upload` and verified against the local
SHA-256 and byte count.

Destination:

- `Tutti` — folder id `1ck40GwTSzctOZjzNQzkY0_JvKD11_U6g`
- parent `GEL_Diagnostics_1920x1080` — folder id `17Wm7WdGoMPVwCHeJ6Se7WBUbgeLL20Cg`
- parent `Overlay Chronon` — folder id `1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`

Files:

- video: <https://drive.google.com/file/d/1ot0OhRJ1Kcg3QN8pxyFoLgGq0QsfYhU6/view?usp=drivesdk>
- poster (`frame0.png`): <https://drive.google.com/file/d/1dxSac68KSJK0zusxeJe3vPpouD4rL984/view?usp=drivesdk>

Upload log:
`RenderingGen/UploadDrive/gel_cinematic_title_closure_20261107/upload_logs/upload_20261107_GEL_Cinematic_Title_Closure_1920x1080_10s.mp4.log`

## Audit status

| item | status | note |
|---|---|---|
| P0 — old still artifact hashes | **documented, not used as oracle** | The older `../gel-final-20261107/README.md` table lists `final.plan.json` `5393581f…` and `final.rgba` `e9546233…`; the files currently in that directory hash to `1f1dcf92d65c6982a89eaf991823d6af97345c6f4cf9e1bb69921f2c9814b320` and `42175be390ca4d6a50c2d53cdf77e0e67b9c9b5c7f2d4608e222e8307f9060ec`. That historical mismatch is not repaired here. This 1080p closure is independently hashed above and does not reuse those files as a byte-exact oracle. |
| P1 — plate alignment | **SUPERSEDED — FAIL** | The blurred backdrop + sharp 1920×251 strip at `[0,0]` produce hard seams at y=415/666. See the revision README; the visual gate is not closed. |
| P2 — real extruded backing | **PASS** | Backings 102–104 carry `extruded_text: true`, `extrusion_depth: 8`, `enable_3d: true`, and 300 resolved camera samples. |
| P3 — motion | **PASS** | Backing and face both carry the 1.035× push over 300 frames; frame 0 and frame 299 were inspected and the encoded 0/5/9.9 s contact sheet is included. |
| 4K/120 | **NOT RUN** | Out of scope for this 1080p closure. |

## Source changes

- `ChrononTemplate/src/chronontemplate/gel/GelCinematicTitlePack.cpp`
  authors the plate, real extruded backings, opaque GEL faces and the
  pixel-locked camera. `backing.material.extrusionDepth` now comes from
  `spec.extrusionDepth`; the backing is 3D and the face is 3D GEL.
- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanLoweringSamples.cpp`
  honors `screen_space` for plane-bound layers while preserving `enable_3d`
  for extruded text.
- `ChrononMotion3D/src/chrononmotion/motion/RenderPlanEmission.cpp`
  serializes screen-space layers with 2D transform vectors and omits resolved
  3D transform samples, so a screen-space layer cannot be re-projected by the
  consumer.
- The sibling `ChrononTemplate` checkout contains the existing contract test
  and the `chronontemplate_emit_gel_cinematic_title` target; the test exits 0.

The source changes are committed in the sibling Chronon3d checkout.
