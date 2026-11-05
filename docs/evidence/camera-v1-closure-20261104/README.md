# Camera V1 closure — four 1080p clips (2026-11-04)

All clips are 1920×1080, 30 fps, H.264 `yuv420p`, CRF 16, no audio, ≤ 6 s.
`ffprobe -count_frames` reports the frame count in the table. Every upload was
verified by `RenderingGen/bin/drive-upload`, which checks the local SHA-256 and
the byte count reported by Drive.

| Clip | Frames | Duration | Bytes | SHA-256 | Drive |
|---|---:|---:|---:|---|---|
| `camera_v1_dof_1080p.mp4` | 150 | 5.000 s | 1,519,202 | `46e21affe2dce8f15d5419bb8b36978a3b3154ea35a38f0ae4a6a4c45cb6e9f2` | [open](https://drive.google.com/file/d/1UuafXLnJuDOBkPXFKAmV2_F4nxjAbNMp/view) |
| `camera_v1_sharp_1080p.mp4` | 150 | 5.000 s | 2,642,790 | `b82b43c63a0f5bfcdf99d09df01a503d1788d4d57571958e3c7684d40ed66fc1` | [open](https://drive.google.com/file/d/1wMGgEZh9BI15k0woM18TZ-48HwaqlyVr/view) |
| `camera_v1_orbit_1080p.mp4` | 150 | 5.000 s | 2,140,353 | `4b0192e1fffad2a57398b5045faaa794e2da1c5bf31c671065434da8a122c09d` | [open](https://drive.google.com/file/d/1f3f4Xx-yn7A_Ad_1s-yRSG0QuHQ_xIgW/view) |
| `camera_v1_cards_breakdown_1080p.mp4` | 180 | 6.000 s | 1,108,733 | `8208a1c671a559fea72ac901b7058444296e22881c2f6f6a72e0844590c21954` | [open](https://drive.google.com/file/d/1f7XUWSFV-rbTwY7Fyxms6mEvb5IBKV0i/view) |

Drive folder: [`1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`](https://drive.google.com/drive/folders/1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS).

The generated MP4s and raw RGBA are gitignored (repo weight policy); the table
above and the Drive copies are the reference. `render.log` and the per-clip
`*.timing.json` sidecars stay in this folder as local evidence.

## Render commands

```sh
CLI=Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
D="$PWD/Chronon3d/docs/evidence/real_optical_assets"
O="$PWD/Chronon3d/docs/evidence/camera-v1-closure-20261104"

# end frame: 149 for dof/sharp/orbit, 179 for cards_breakdown
"$CLI" render --plan "$D/real_optical_${m}_1080p.plan.json" --assets-root "$D" \
  --backend software --start-frame 0 --end-frame "$end" \
  --video-sink raw --output "$O/${m}.rgba" -v error

ffmpeg -y -f rawvideo -pix_fmt rgba -s 1920x1080 -r 30 -i "$O/${m}.rgba" \
  -c:v libx264 -pix_fmt yuv420p -crf 16 -preset medium \
  "$O/camera_v1_${m}_1080p.mp4"
```

Per-clip render wall time (raw sink, software) and peak RSS from
`/usr/bin/time`: dof 261.94 s / 1,168,628 KiB; sharp 180.66 s / 1,103,824 KiB;
orbit 248.43 s / 1,469,396 KiB; cards_breakdown 129.25 s / 1,174,072 KiB.

## Non-vacuity

Decoded frame 75 of `dof` versus `sharp` on the 0–255 scale: **MAE 5.970,
p99 47, max 156, 1,072,100 pixels changed by more than 2**. The DoF pair is
therefore not the same render.

## Test verification

```sh
ctest --test-dir Chronon3d/build/chronon/linux-video-release \
  -R '^(chronon3d_(camera|core|render_graph|renderer_core)_tests)$' \
  --output-on-failure
```

Result: **4/4 PASS in 156.53 s** — `chronon3d_camera_tests` 13.46 s,
`chronon3d_core_tests` 86.02 s, `chronon3d_render_graph_tests` 30.33 s,
`chronon3d_renderer_core_tests` 26.68 s.

```sh
ctest --test-dir ChrononMotion3D/build/chronon/dev \
  -R 'chrononmotion_(camera_v1|camera_focus_sequence|camera_trajectory|camera_shake|camera_framing|camera_sequence|documentary_camera_path|authoring|advanced_authoring|boundary)$' \
  --output-on-failure
```

Result: **10/10 PASS in 34.27 s**.

## Limits

- Software backend only; no Vulkan strict-native render in this set.
- Local dirty trees; no same-SHA CI, excluded from this closure by owner
  decision (2026-11-04).
- No audio track; v1 H.264 CRF 16, from raw RGBA8.
- C7 (human review of the full sequence) and the Mixed 3D/native corpus remain
  open in the master; they are not certified here.
- The four archived tickets keep pre-existing links to GPU logs and media that
  are gitignored or absent from this checkout; those links are not new and were
  not fabricated.
