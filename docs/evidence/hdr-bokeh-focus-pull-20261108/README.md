# HDR bokeh focus pull — 1920×1080, 30 fps, 180 frames

Native C++ composition `HdrBokehFocusPull`, rendered in a **single process** with
the Vulkan backend, the real CameraRing effect (64 taps) and an ACES tone map at
the output boundary.

## What the scene does

- 30 deterministic scene-linear emitters with RGB values up to 12.0 (working
  space luminance far above 1.0; the composited frame peaks at 194.07).
- `blur.camera_ring` with 64 golden-angle taps plus per-pixel jitter; the
  optical center sweeps sideways and the CoC breathes, which is the visible
  focus pull. The emitter layer is `screen_space()` so the native Vulkan
  ellipse path runs instead of the 2.5D projection.
- Authored 2.5D camera drift and a real `focus_distance` rack.
- Compression is **not** baked into the scene: `--tone-map aces` applies
  `color::tone_map_framebuffer` (ACES fitted) before the linear→sRGB/YUV
  conversion on both encoder lanes.

Source: `src/scene/utils/hdr_bokeh_focus_pull.cpp`.

## Render command (one process, 0–179)

```bash
ROOT="$PWD"
CLI=Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli
"$CLI" render HdrBokehFocusPull \
  --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --assets-root "$ROOT/Chronon3d" \
  --tone-map aces --exposure 1.0 \
  --encoder-backend native \
  --frames 0-179 \
  -o RenderingGen/hdr_bokeh_focus_pull_20261108/hdr_bokeh_focus_pull_aces_1080p30.mp4
```

## Output

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `RenderingGen/hdr_bokeh_focus_pull_20261108/hdr_bokeh_focus_pull_aces_1080p30.mp4` (canonical, single process) | 2,391,744 | `f84800c83dfb6a113de3ea26a4b81528a82433b13bb8d6f41cd480b239d17f9a` |
| `..._two_segment_concat.mp4` (previous 90+90 lossless concat, provenance) | 2,408,808 | `a31d721d50b1f8212675d612410849991b0df53ba6a1905b42e52a6876ac94c8` |
| segment 1 `0–89` | 1,200,206 | `a54d79a96a67d479f0c29a9592883678623aeb90949c065f6874dff043207f0c` |
| segment 2 `90–179` | 1,209,211 | `3705a7d38b57b8a46d45dac3f7c259fe573389edcab357882fe541f5c8082736` |

`ffprobe` on the canonical file: H.264 High, yuv420p, 1920×1080, 30/1 fps,
`nb_read_frames=180`, duration 6.000000 s.

The canonical file and the concat decode to the same pictures: PSNR
`y=53.82 dB, u=56.13 dB, v=56.06 dB, average=54.46 dB, min=47.87 dB` (the
difference is only the re-encode boundary). Contact sheet regenerated from the
canonical file:
`RenderingGen/hdr_bokeh_focus_pull_20261108/final_contact_180_single.png`.

## HDR / tone-map evidence

`[native_av] tone-map=aces exposure=1 …`, one sparse line every 30 frames:

| Frame | max_linear_luma | max_mapped_luma |
|---:|---:|---:|
| 0 | 30.2331 | 1.0000 |
| 30 | 71.3102 | 1.0000 |
| 60 | 95.6477 | 1.0000 |
| 90 | 144.0528 | 1.0000 |
| 120 | 194.0686 | 1.0000 |
| 150 | 64.9365 | 1.0000 |

Composition-side checks from the same build: source `max=345.93588`, EffectStack
`max=112.85424`, composite `max=90.84025`. The highlight range is real; the tone
map is what brings it back to SDR.

## VRAM: corrected root cause of the earlier frame-159 failure

The first 0–179 attempt failed at frame 159 with
`vmaCreateImage failed with VkResult=-2` in `LayerDoF`. That was **not** a leak
in this scene. Per-PID sampling with `nvidia-smi` shows:

| Run | Our process VRAM | GPU total during the run | Result |
|---|---:|---:|---|
| 0–39 probe | 656–1328 MiB | 7.9–15.0 GiB | exit 0 |
| 0–179 single process | 784–1464 MiB | 7.5–15.6 GiB | exit 0, 180 frames |

Vulkan telemetry is flat for the whole 180-frame job:
`vram_bytes=522027008`, `live_surfaces=10`, `physical_images=10`,
`vma_allocations=79` at frames 25…175. The GPU was nearly exhausted by
**concurrent renders from another job** (`06_dust_field_native` holding ~8.5 GiB),
so the allocation failed while free VRAM briefly dropped below the required
surface size. With the same code, the complete 180-frame render succeeds in one
process as soon as it gets allocation headroom.

The `LayerDoF` RAII release added earlier is still required: it frees the three
node-local full-canvas surfaces (`expanded`, `depth`, `scratch`) per node
instead of leaking them into subsequent frames.

Known, bounded bookkeeping: the runtime surface registry accumulates ~7
`FrameTransient` identities per frame (178 → 1228 handles over 180 frames) and
releases them at job teardown. It does not retain Vulkan images or VRAM.

## Tests

- Build: `cmake --build build/chronon/linux-video-release -j4` → `BUILD_EXIT=0`
  (RAM checked before building; never `nproc`).
- `chronon3d_core_tests`, filter `*tone*,*Tone*,*OutputTransform*`: 3/3 cases,
  12/12 assertions, SUCCESS.
- `chronon3d_effects_tests`, filter `*CameraRing*`: 7/7 cases, 781/781
  assertions, SUCCESS.

## Drive delivery

`DRIVE_UPLOAD_PASS` with the pinned local SHA-256 and byte count:

- File: [HDR_Bokeh_Focus_Pull_1920x1080_6s.mp4](https://drive.google.com/file/d/12tlS7g2t6GRaGajsChb8tgUeIC5EON4p/view?usp=drivesdk)
- ID: `12tlS7g2t6GRaGajsChb8tgUeIC5EON4p`
- Parent: `1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS`
- SHA-256: `f84800c83dfb6a113de3ea26a4b81528a82433b13bb8d6f41cd480b239d17f9a`
- Bytes: 2,391,744

Staged copy and manifest:
`RenderingGen/UploadDrive/hdr_bokeh_focus_pull_20261108/`.

The six `CameraRing Text` v2 clips in
`RenderingGen/UploadDrive/camera_ring_text_20261108/` were not touched; hashes
are recorded in `camera_ring_v2_sha256.txt`.

## Limitations

- The still/PNG lane is not pixel-truth for Vulkan; the video lane is the
  authoritative evidence.
- The VRAM conclusion is measured on this RTX A4000 (16 GiB) with a concurrent
  render; other scenes can have larger footprints. The engine has no per-job
  admission retry for transient external GPU pressure, so a render sharing the
  device with a heavy job can still fail allocation.
- No commit/push was made; render artifacts stay outside the source tree.

## Files

- `render_single_180.log` — full log of the canonical single-process render.
- `vram_single_180.csv` — per-2 s `nvidia-smi` samples (PID MiB, GPU total MiB).
- `vram_probe_40.csv` — the 40-frame probe samples.
- `psnr_single_vs_concat.txt` — per-frame PSNR of canonical vs concat.
- `camera_ring_v2_sha256.txt` — untouched v2 clip hashes.
