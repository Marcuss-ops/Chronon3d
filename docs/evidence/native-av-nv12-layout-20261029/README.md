# NVENC host staging layout correction — 2026-10-29

## Scope and source state

This follow-up fixes the current-source software-frame → H.264/NVENC export
corruption recorded in `ChrononMotion3D/docs/evidence/ticket-roundtrip-current-source-20261029/README.md`.
The CUDA frame pool declares `AV_PIX_FMT_NV12`; the host staging frame and
`av_hwframe_transfer_data` source now use NV12 as well. The previous code
converted RGBA into planar YUV420P and transferred that layout into an NV12
hardware frame, even though `av_hwframe_transfer_data` does not perform a
pixel-format conversion.

- Chronon3D HEAD: `014eeb4dfd1703820d49b6382d127ab253bf1449` (dirty working tree).
- Build: `cmake --build build/chronon/linux-video-release --target chronon3d_cli -- -j7` — exit 0.
- Host: Linux x86_64, NVIDIA RTX A4000; software renderer, H.264 `h264_nvenc` encoder.
- Fixture: ChrononMotion3D animated glTF plan, frames 0–30, 640×360.
- The NVENC route bypasses the converted-frame cache while it allocates a new
  host staging frame per input; this prevents cache hits from leaving a fresh
  staging frame uninitialized.

## Reproduction and verification

Default route (program-route override unset):

```sh
env -u CHRONON3D_VIDEO_PROGRAM_ROUTE \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
  --plan "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf/animated-gltf.plan.json" \
  --assets-root "$PWD/../ChrononMotion3D/docs/evidence/ticket-roundtrip-scaled-20261029/gltf" \
  --backend software --profile preview --hardware nvenc --codec h264_nvenc \
  --start-frame 0 --end-frame 30 \
  --output /tmp/chrononmotion-gltf-current-source-nv12-final.mp4 -v info
```

The explicit canonical composition route (`CHRONON3D_VIDEO_PROGRAM_ROUTE=0`)
was also run; its decoded output was byte-identical to the default route.
Both exports accepted 31 frames and wrote 31 packets. FFmpeg decoded the MP4
to 31 RGBA frames, all with distinct frame hashes.

| Artifact | SHA-256 |
|---|---|
| Software raw RGBA reference | `a8d12571e3cc657b7a847dfc05a8888c766b6d9658aecbae3a18c65c88fe2648` |
| Default-route NVENC MP4 (9,662 bytes) | `35530c11cc166c755883f98d58b1fe1a4a09928c69b2ca3af696e1d9b6177cf7` |
| Decoded default-route RGBA | `9d2e9f37ce567b69c3b524d8f33193422c51873f0a2e78ce01dd683ee67aa9c6` |

H.264 has no alpha plane, so comparison composites the software RGBA reference
over black. Across the complete decoded sequence, RGB RMSE is `0.7673`, mean
absolute error is `0.0361` (0–255 scale); frame 0 has 2,250 nonblack pixels
around the reference's 1,642 opaque subject pixels. The remaining small error
is concentrated around edges after 4:2:0 chroma subsampling. The prior green
plane corruption is gone and the animated textured subject is visible.

Visual artifacts uploaded to the requested Drive folder:

- [Chronon3D NVENC full frame](https://drive.google.com/file/d/1WLNb7-Ckv3f08xIxmXDYpmftENK6iWvw/view?usp=drivesdk) — original 640×360 MP4 produced by Chronon3D.
- [Chronon3D NVENC zoom](https://drive.google.com/file/d/1NPlBUfDuJxITh8DEnK7FXt2XJKyR3ghe/view?usp=drivesdk) — crop and nearest-neighbor upscale of the encoded MP4 for visual review.

## Remaining gates

This closes the host-frame NV12 layout mismatch for the tested software-render
→ NVENC route. It does not prove strict-native Vulkan mesh drawing, GPU-render
parity, all codec/color configurations, or clean-SHA CI. Those remain open in
the glTF and inline-mesh tickets.
