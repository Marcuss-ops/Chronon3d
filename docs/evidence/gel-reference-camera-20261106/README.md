# Gel reference camera sweep

Vulkan render of the supplied 1094×143 reference plate with the existing MTSDF gel material, background transmission, glow layers, and a four-second camera move. This is an implementation/evidence render, not a final match: the plate is static, and depth of field is disabled in this sweep.

## Outputs

- `gel-reference-camera-4s.mp4`: 120-frame H.264 preview, 1094×143 at 30 fps.
- `gel-reference-camera-sequence.rgba`: strict Vulkan raw output, 120 frames, 75,092,160 bytes.
- `frame000.png`, `frame060.png`, `frame119.png`: start, midpoint, and end samples.
- `reference-camera.plan.json`: exact render plan.
- Timing sidecars: per-frame timing data from the renderer.

The cyan title centroid moves from x≈670 at frame 0 to x≈497 at frame 119; the phrase stays inside frame and remains legible. Repeat strict-Vulkan raw export compared byte-for-byte with SHA-256 `aad0bda75d507c68f6068e083e90d1a34b8eb4c9d797c8e05b9623cae99099cf`.

SHA-256:

- MP4: `0534292423e1359586a41b1f14c0713f28265f15ad4e2a3d1aac2f999c57071b`
- Raw RGBA: `aad0bda75d507c68f6068e083e90d1a34b8eb4c9d797c8e05b9623cae99099cf`
- Midpoint PNG: `f09cd6b4dbe2dea01194901d16febb511220db63afca9b14ec9fa8a416e6d3b6`

## Verification

Built `chronon3d_cli`, `chronon3d_c_abi_tests`, and `chronon3d_text_tile_raster_canary_tests` using `cmake --build build/chronon/linux-video-test --target chronon3d_c_abi_tests chronon3d_text_tile_raster_canary_tests chronon3d_cli -j12`.

- GEL decoder/fingerprint cases: 2 passed, 41 assertions.
- Vulkan text tile raster canary: 1 passed, 105 assertions.

The schema now accepts `style.gel_soft_fusion` in Render Plan v2 and v3. The build emitted existing CMake developer warnings and FFmpeg deprecation warnings; no build errors.
