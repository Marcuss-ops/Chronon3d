# GEL-TEXT diagnostic stress run — 2026-11-05

This is executable evidence for the priority P0 comparisons. It records what
ran and what remains unavailable; it is not a visual approval of the complete
gel treatment.

## Run contract

- Reference tree: `533b6ab8` (`main`); the checked out tree was dirty and already
  contained the GEL shader WIP. Its current diff digest is recorded in
  `run-manifest.json`.
- Device: NVIDIA RTX A4000, driver 595.99.02, Vulkan 1.4.329. The CLI reported
  Vulkan text dispatch on device 0. `--backend vulkan` is the explicit backend;
  there was no auto backend selection. The raw sink readback/conversion is part
  of artifact export, not a CPU rendering fallback.
- Canvas: 1280×720, one frame at 30 fps, dark uniform backdrop, centered text
  `calories or ingredients`.
- Font: `assets/fonts/Inter-Bold.ttf`, SHA-256
  `deb990e241954e1af87c6033d40a76cf0f48e0d112541794526051ac4a3ee2c8`.
- Text size and position: 90 px; 1200×220 layer at (640, 360). All plans use
  the same string, font, size, layer bounds, position, canvas and backdrop.
- Shader: `src/backends/vulkan/shaders/text_tile_raster.comp`, SHA-256
  `877be16d9479f38402f94d342fec6730c75768e9fbeb5f0064e6b3222be12903` (after mode-5 T14 diagnostic).
- Command shape:

  ```bash
  build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli render \
    --plan docs/evidence/gel-text-stress-20261105/T00.plan.json \
    --assets-root "$PWD" --backend vulkan --start-frame 0 --end-frame 0 \
    --video-sink raw --pipe-pixfmt rgba \
    --output docs/evidence/gel-text-stress-20261105/T00.rgba -v info
  ```

  Each case has its exact plan, log, timing sidecar, raw RGBA and PNG. PNGs
  were derived only with `tools/raw_frame_to_png.py`, which converts the raw
  sink's premultiplied alpha to straight-alpha PNG.

## P0 results

| ID | Status | Evidence and finding |
|---|---|---|
| T00 | PASS | Flat raw is legible/centered; the new `.frame_0.glyphs.json` sidecar records all 23 shaped glyph positions, advances, byte clusters, font, baseline and world matrix. The raw remains byte-identical to T01. `glyph-layout-invariant.json` confirms exact coordinate/layout identity across T00–T08. |
| T01 | PASS | Flat white coverage frame has no visible atlas rectangles or lost counters. T00 and T01 raw SHA-256 are identical (`42f9f3a8…81f9`); their thresholded silhouette IoU is 1.0. |
| T02 | PASS_LIMITED (one 720p pixel-grid oracle) | Composite SDF vs independent signed Euclidean distance transform of T01 coverage: IoU 0.996591; in distance band [−3,+4] px MAE 0.230 px, p95 0.458 px, 98.24% within 0.5 px. This is not a subpixel vector-outline or transform sweep. See `T02-distance-oracle-metrics.json` and its error PNG. |
| T03 | PASS (visual profile) | Height-only debug PNG shows filled domes. On a one-pixel-eroded T01 core the linear height ranges 0.144–1.0, median 0.694 and p95 0.939. See `field-metrics.json`. |
| T04 | PASS | Mode 4 classified every pixel in the one-pixel-eroded T01 core green (18,806/18,806 finite and `abs(length(N)-1) < 0.01`; zero red/other). The mode-3 normal map shows continuous gradients without visible stitching at this scale. See `T04-validation-metrics.json`. |
| T05 | PASS | The core-only diagnostic renders filled cyan letters, not a contour-only stroke. Against the flat frame, the thresholded silhouette IoU is 0.996613 and both boxes are `[189,323,1089,409]`. |
| T06 | PASS (single frame) | Lit gel with diffuse plus broad/sharp specular and bloom off shows the highlight on a filled face. T06 raw is byte-identical to T07-off; the reflection sweep itself remains P1 T12. Compare `T05.png`, `T06.png` and `T06-difference-overlay.png`. |
| T07 | PASS | Same gel material, only the two layer-glow effects change. The halo extends the low-threshold box from `[189,323,1089,409]` to `[178,313,1100,420]`; on 8,048 pixels at least 5 px inside the baseline core, the RGB difference is exactly zero. `T05-difference-overlay.png` and `T07-difference-overlay.png` mark changed pixels red. |
| T08 | PASS (technical 720p frame) | Layer shadow and chromatic aberration render; the title remains legible and centered. Against T07-on the core alpha matches exactly on 8,002 eroded pixels; see `T08-metrics.json` and `T08-difference-overlay.png`. Human reference review is still required for overall GEL-VISUAL. |

T02 and T03 have diagnostic PNGs; T02 now also has a pixel-grid Euclidean distance oracle against the flat coverage mask. T04 normal validity is PASS on the measured core. T00 now emits per-frame glyph coordinates; all T00–T08 dumps have identical layout hash, glyph positions and world matrix. Overall `GEL-VISUAL` remains NOT RUN pending reviewer comparison against the supplied reference.

### M1 distance-oracle sample — 2026-11-06

`tools/gel_sdf_oracle.py` independently derives a 0.5-coverage mask from T01 and compares the T02 debug distance after decoding its sRGB red channel. The T02 radius is 7.3125 px (`5.2 × 90/64`, scale 1). The pixel-center signed Euclidean distance transform reports 32,393 samples in [−3,+4] px: MAE 0.230 px, median 0.220 px, p95 0.458 px, maximum 1.611 px; 98.24% are within 0.5 px and 99.85% within 1 px. Signed-region IoU is 0.996591. The T04 mode-3 normal map also agrees in direction with the negative oracle SDF gradient across 16,465 core-band samples: mean cosine 0.99595, 5th-percentile cosine 0.98131, median angular error 2.22°, p95 11.10°, and no opposed normals. T04 mode 4 independently reports all 18,806 eroded-core normals finite and normalized. `T02-distance-oracle-error.png` visualizes absolute distance error (white is the mask boundary). [Open the heatmap on Drive](https://drive.google.com/file/d/1g_EbQTKV1y49DM72oW7PgMr8jzOj2b_v/view?usp=drivesdk). This remains a limited raster-grid oracle: the boundary is inferred from quantized coverage pixels, and no subpixel vector-contour reference or zoom/rotation sweep is claimed. M1 remains open for that broader validation.

## P1 status

| ID | Status | First failing pixel/frame | Component / reason |
|---|---|---|---|
| T09 Topology | PASS (visual sample only) | N/A | `%&@$# æøå gggQQQ 88888888` rendered in `T09.png`; counters/openings visible. No automated counter/topology oracle. Raw SHA and plan hash are in the manifest. |
| T10 Kerning | FAIL (observed frame 30) | Frame 30, tracking −30 px; sampled coverage bbox shrinks to x=189–428 (240 px) and the phrase visibly collapses/overlaps in `T10-030.png`. | Tracking/layout animation. 61-frame raw and frames 0/30/60 are saved. Current glyph sidecars capture the base TextRun snapshot, not evaluated animated positions, so they are not used to measure animated offsets. |
| T11 Subpixel/zoom | PASS (continuity only) | 61 frames; no zero-coverage frames after affine-transform fix. At maximum zoom, text can leave the viewport. | `T11-transform-fix.rgba` and `T11-transform-fix-metrics.json`; optical shimmer review remains open. |
| T12 Light sweep | PASS (parameter response only) | 61-frame azimuth sweep; 26,369 RGB pixels change in the ROI between frames 0 and 30. | `T12-light-sweep-*.png`, raw and metrics; optical match review remains open. |
| T13 Camera 2.5D | PASS (geometry invariant) | Across 61 frames, flat and gel alpha coverage is byte-identical (0 XOR pixels); sampled camera path uses dolly/orbit and return with fixed near/far/focus. | `T13.rgba`, `T13-gel.rgba`, plans and `T13-metrics.json`; this tests a small camera move on a 3D-enabled text plane, not a broad camera/DoF matrix. |
| T14 Alpha/transmission | PASS (stylized, limited) | Strict Vulkan raw ON/OFF: 26,534 RGB pixels differ inside the text ROI, none outside; alpha unchanged. | CompositeNode applies per-channel Beer–Lambert attenuation to backdrop RGB. Optical depth is approximated from source alpha; no independent physical thickness buffer. See `T14-destination-transmission-metrics.json` and the strict-readback captures. |
| T15 Temporal/seek | PASS (same device/build/route) | No mismatch: two 150-frame linears hash identically; eight seeks in order 90,0,149,45,45,149,0,90 match the linear reference and repeated same-frame hashes. | Deterministic for this animated tracking plan and device; metrics/hashes in `T15-seek-metrics.json`. |

## P2 status

P2 performance is **NOT RUN** because T10 fails and P1 is incomplete. Earlier smoke
clips and 4K probes under `docs/evidence/gel-text-vulkan-smoke-20261105/` use
separate plans and do not satisfy this run's invariant set. Sidecars expose an
aggregate device execute span for individual runs, but not the per-stage GPU
percentiles, NVENC/readback breakdown, or 4K/120 end-to-end result required by
P2. No component failure or first failing pixel/frame is claimed for unrun IDs.

## Build and canary

The requested build parallelism is `-j14`. `linux-video-test` initially referenced a missing `/tmp/clipper2-overlay`; its cache was corrected to use repository overlay ports and vcpkg rebuilt Clipper2. The CLI, Vulkan canary and C ABI targets then built in 350/350 steps. The C ABI gel filter exposed a stale invalid-mode expectation (mode 4 is now valid); after changing it to reject mode 6, the rebuilt filter passes 2/2 cases and 34/34 assertions, including transmission ablation and mode 5 destination diagnostics. The Vulkan GPU canary passes 1/1 case and 105/105 assertions; renderer-core gel filter passes 7/7 cases and 742/742 assertions. The release cache was also reconfigured to the populated test vcpkg packages; no separate release build result is claimed. `tools/verify_text_tile_gpu_canary.sh` now defaults to the measured 105 assertion contract.

`python3 tools/raw_frame_to_png.py --self-test` passed. The canary command was:

```bash
JOBS=14 CHRONON3D_REQUIRE_TEXT_CANARY_DEVICE=1 \
tools/verify_text_tile_gpu_canary.sh
```

See `run-manifest.json` for plan/raw hashes, command parameters, device,
timings, and dirty-tree digest.

## Review proxy

`GEL-TEXT-P0-review-proxy-20261105.mp4` is a 13.2-second, 1280×720, 30 fps sequence of raw-derived T00/T01/T02/T03/T04 normal/T04 validity/T05/T06/T07/T08 PNG stills. The captions identify each diagnostic. It is for visual review only; H.264 frames are not pixel oracles. SHA-256: `c9fe7554689b23af1c36e1c496feb0972f3e6912005f394ff35a312374dce3a8`. Uploaded to the requested [Google Drive folder](https://drive.google.com/drive/u/1/folders/1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS); Drive file: [GEL-TEXT-P0-review-proxy-20261105.mp4](https://drive.google.com/file/d/145m2uSOYH5vPpXaGxL4-ClXaxcG_iBl3/view?usp=drivesdk).

## T04 validity result

The explicit Vulkan rerender of `T04-validation.plan.json` completed successfully (GPU device execute span 0.290 ms; frame render 32.929 ms, raw export conversion/readback separate). Raw SHA-256: `875c2f3e777ca0e9024a0378764f749484daf21dc2519baeb69e952ffa12d2d3`. The shader validity diagnostic reports 18,806/18,806 pixels green inside the 3×3-eroded T01 coverage mask, zero red and zero other pixels. See `T04-validation.png` and `T04-validation-metrics.json`.

## Glyph geometry dump

`--debug-text-layout-json <path>` now emits a video/raw-route sidecar `<path>.frame_<n>.glyphs.json` from the same RenderPlan TextRun snapshot. It records source text, font path/size, layout hash, shaped positions/advances/byte ranges, animated offsets and world matrix. With the exact `calories or ingredients` phrase, the 23 visible glyph records match across T00–T08; canonical geometry digest: `8c0f2640ceb60bb97a7c5fcd90e7278ad6c8e39e88bc7265018488bbbcc68002`. The T00 probe raw SHA is unchanged (`42f9f3a8…81f9`).

## P1 evidence update

Historical initial P1 checkpoint (superseded by the dated follow-ups below): T09 was a visual sample only; T10 showed phrase collapse at the −30 px tracking sample; T11 had blank coverage during zoom; T12 lacked animated gel-light control; T13 passed its sampled camera geometry invariant; T14 ON/OFF was identical before destination-aware transmission was implemented. Current measured statuses are in the table above and later sections.

T15 used the same 150-frame animated tracking plan for two full linear renders and eight isolated seeks in order 90, 0, 149, 45, 45, 149, 0, 90. Both linear raw hashes match, each seek raw frame matches its corresponding linear frame, and repeated seeks of the same frame are byte-identical. This covers deterministic rendering on this device/build/route; it does not substitute for the unrun light sweep, broader camera/zoom validation, or a working transmission result.

T11 exercised scale 0.05×→10×→0.05× over 61 frames and Z rotation through +15°/−15°. Raw coverage analysis found frame 19 (and subsequent sampled frames through 29) completely blank (zero pixels over the RGB>40 foreground threshold), while frame 30 returned to 23,519 foreground pixels. Treat as a temporal/transform FAIL; the exact clipping/transform stage is not isolated. `T11-metrics.json` contains per-frame bounding boxes and coverage.


The P1 review proxy contains raw-derived stills for T09, T10 tracking samples, T11 zoom/rotation including the blank frame, T13 flat/gel camera, and T14 transmission ON/OFF plus mode 5 flag/destination diagnostics. It is 22.567 seconds long and captioned for review and is not a pixel oracle. [Open GEL-TEXT-P1-review-proxy-20261105.mp4](https://drive.google.com/file/d/1EqyY5kb8SvO0RNshiJqGyT14DMpFgiGK/view?usp=drivesdk). SHA-256: `b483ec3dc62bdd25e480ebc6ed8c506597a25b9310675e8461c3b813eddc904f`.

T13 rendered flat and gel variants over the same 61-frame perspective camera path. The text plane is 3D-enabled; near/far and focus remain fixed while dolly/orbit samples change and return. Alpha coverage matches exactly on all frames (zero XOR), verifying that gel changes shading without changing projected geometry for this camera path. See `T13-metrics.json`; the scene still needs reviewer comparison for aesthetic/camera quality.

Historical first T14 attempt: the explicit `gel_transmission` RenderPlan ablation decoded and hashed distinctly, but ON/OFF Vulkan raw outputs were identical. Debug mode 5 showed that the TextRun raster executes before the backdrop is composited. This diagnosed the original pass-order gap; destination-aware transmission and the successful strict raw rerun are documented below.

Debug mode 5 is `R=gel_transmission flag`, `G=destination alpha`, `B=0`; it was added for T14 route diagnosis. Shader SHA-256 after this addition: `877be16d9479f38402f94d342fec6730c75768e9fbeb5f0064e6b3222be12903`. The diagnostic-on raw SHA is `bc42e00c29d16f96ae6532cbc0cad17a58060cac730b36ff8859a1ad2972c783`; off is `f6801f01a7227b00eaf2d917081b3a139b9212ff4099995f50ac5c656cbb8bc1`.

## Material relief iteration — 2026-11-06

A single-variable follow-up to T06 keeps the same TextRun, glyph coordinates,
font, plan, camera, and 1280×720 Vulkan/raw route. The shader now uses a 5.0
relief-normal scale (previously 1.0), stronger diffuse contrast, and a broader
specular response. Build command used `-j14`; the one-frame GPU dispatch span
was 0.290 ms. The raw SHA-256 is
`f8ea5a4adec8afea1ac6246419a95224c91c49a878a9534871ef14e1c5ffdfe1`; shader
SHA-256 is `b9aa95daccbddb0b759b23fbdeb62df35030ea2cfeb68cd1ff7f4bfb69799b38`.
The raw-derived image is `T06-relief-5.png`; `T06-relief-5.run.json` records
commit, dirty diff digest, device, plan/font/shader hashes and exact command.
The 5-second before/after review proxy is
`GEL-TEXT-relief-iteration-20261106.mp4` (SHA-256
`e3e4b2b367c52d3eb5231c99036c7db368f296f29ab0907577909f4dfc6137bf`), uploaded
to the requested Drive folder. This is a material-only prototype with stronger
apparent dome shading. It is not yet reviewer-approved, and it does not add an
extruded mesh or certify 2.5D camera depth; those remain separate work.


### RGB Beer–Lambert + updated review video — 2026-11-06

The Vulkan text shader now attenuates the lit GEL body per RGB channel through the dome using sigma `(1.2, 0.4, 0.05)`, leaving surface specular and Fresnel response unattenuated. Filtered GEL tests pass 8/8 cases and 748/748 assertions; the full renderer core suite passes 217/217 and 73,361/73,361, and the strict-device Vulkan canary passes 105/105. `T06-absorption.png`, the three `T12-absorption-*.png` samples and `beer-lambert-rgb-render-metrics.json` are derived from Vulkan raw captures. T14 RGB ON/OFF differs at 26,534 pixels in the text ROI while alpha remains byte-identical.

Review clip (5.07 s, 1280×720, 30 fps): [GEL-TEXT-rgb-absorption-review-20261106.mp4](https://drive.google.com/file/d/1NY307ykWxvHeuymRN5nELNIfldehXkf0/view?usp=drivesdk). It shows the static GEL material, animated light sweep and depth-of-field rack focus. The clip is a review proxy, not the raw pixel oracle.

### T14 destination-aware transmission — 2026-11-06

The existing CompositeNode selects a channel-dependent GEL backdrop-transmission branch when the TextRun material enables `gel_transmission`; ordinary layers keep standard source-over. Vulkan and software paths share an alpha-derived Beer–Lambert approximation. On the fixed checkerboard plans, `T14-on-transmission-fix.rgba` versus `T14-off-transmission-fix.rgba` changes 26,534 RGB pixels inside the text ROI and zero alpha or outside-ROI pixels. Vulkan composite unit coverage passes 10/10 assertions; raw captures, PNGs, run logs, test logs and machine-readable metrics are in this directory. This is not a fully physical transmittance buffer: source alpha still combines text coverage, authored opacity and the red-channel thickness proxy.

Strict raw export is now verified too. With `--backend vulkan --gpu-hot-path-mode require_gpu_native --video-sink raw`, raw jobs retain a native render slot and the writer explicitly downloads its device-local output. Both T14 ON/OFF runs completed at 1280×720; their hashes match the prior raw evidence byte-for-byte. See `T14-{on,off}-strict-readback.{rgba,png,log}`. The earlier failed attempt remains in `T14-on-strict-gel.log`; it is superseded by the successful strict reruns.

Open certification gates remain: full M1 distance-oracle review, M2 depth diagnostics/occlusion tests, M3 optical comparison and human visual acceptance, M4 bokeh shape/occlusion diagnostics, and P2 performance. The five-module ticket remains OPEN.

### T18 — 2.5D reference-style review — 2026-11-06

Strict-native Vulkan/NVENC H.264, 1094×143, 120 frames at 30 fps. The title
uses the existing GEL TextRun shader on a 3D-enabled plane; the camera sweeps
from left to right with a shallow push-in, over a new static generated waiting
room plate. A dark navy drop shadow sits behind the text, with two cyan/blue
glow layers. The Drive folder contains the [4-second preview](https://drive.google.com/file/d/1ihWxf2Et4l2mLzZBq9sGaYUfc3QLMhi9/view?usp=drivesdk),
[raw-derived frame 60](https://drive.google.com/file/d/1sHC1HOoy-rCRP8DM_h-fOSJGEsrBVNTO/view?usp=drivesdk),
manifest, plan, raw RGBA frame, and background plate. The background is a
generated still, not original footage. This is a 2.5D visual review candidate,
not physical extrusion or a final human visual PASS. Depth dumps/occlusion,
bokeh shape, optical approval, and P2 4K/120 remain open.

### T16 Vulkan circular Vogel DoF rerun — 2026-11-06

Inspection of the native global `PerPixelDofNode` found it dispatched two
separable passes, while the existing depth-aware circular kernel was not used
on this route. The node now calls `dof_blur_disk_surface()` once; its shader
samples a deterministic Vogel disk (16–128 samples based on maximum CoC) and
keeps the existing per-sample depth tests and foreground scatter behavior.
This reuses the authoritative depth provenance and kernel API.

The T16 DoF-on plan was rendered for all 61 frames at 1280×720 through
Vulkan/raw. `T16-vogel-on.rgba` SHA-256 is
`35912933e9a6b46a322e33c4be8fc37d12e33b680f897dbf99da9ec85ffcdd92`; shader
SHA-256 is `9514cf38260423229cee5a3cabcf146a3b59aceafb828c4649daf5dafef56294`.
Raw-derived samples are `T16-vogel-on-{0,30,60}.png`, timing is in
`T16-vogel-on.rgba.timing.json`, full provenance is in
`T16-vogel-on.run.json`, and the 2.034-second H.264 review proxy is
`T16-vogel-on-review.mp4` (SHA-256
`061e1b0cc31983bacf7b6a70b994999f62db59105c82b9d3719910a0cd72e4aa`). [Drive
review](https://drive.google.com/file/d/1lMMy6nD_zHg6-5X_O_McfFVfyRYFn7Bo/view?usp=drivesdk).
Compared with the previous separable-kernel T16 raw, 5,060,783 RGB pixels
change across the sequence (mean absolute channel delta 0.997475/255; maximum
57). GPU device-span p50/p95/max is 2.1432/2.1832/2.1883 ms. Render-thread
p50/p95/max is 69.941/92.714/444.232 ms; 61/61 frames exceed the 33.33 ms
30-fps budget. The measured FPS is 8.33, including the render/export path.
This demonstrates the circular-kernel dispatch and changing output, but does
not certify optical bokeh shape: no isolated point-highlight test is in T16.
The kernel patch passed the full renderer-core CTest and the Vulkan text
canary after test-cache rebuild with `-j10`; release CLI build completed
578/578 steps with `-j10`. The aggregate renderer-core CTest took 32.07 s;
the Vulkan text canary passed 105/105 assertions.
Depth visualization/occlusion, human optical review, and 4K/120 remain open;
P2 is still FAIL/not certified.

### T16 exact circular bokeh shape check — 2026-11-06

The isolated Vulkan point-highlight fixture found a sparse-support defect in
the Vogel kernel: the 8 px CoC response lit only 22.2% of the expected disk at
20% of peak. The replacement gathers the exact circular pixel support and
matches the existing CPU disk predicate. The new GPU readback measures 94.7%
disk occupancy and zero lit square-corner pixels. Evidence: the captured
10× grayscale GPU response is `T16-point-bokeh-vulkan.png`; test output and
counts are `T16-exact-disk-canary.log`. The CTest target passes 1/1 with 2/2
cases and 32/32 assertions. The direct run with optional PGM capture passes
2/2 cases and 34/34 assertions. The PNG was uploaded to the requested
[Drive folder](https://drive.google.com/file/d/1OwztiO8thJiJ6hslrczL0LGx6lyVJ_qb/view?usp=drivesdk).

The exact-kernel T16 rerender completed 61/61 1280×720 frames. Its raw hash is
identical to the previous T16 sequence, so that text scene does not exercise
the isolated point-response difference. Updated p95 timings are 2.187 ms for
the full GPU batch, 1.180 ms for the blur pass, and 150.190 ms on the render
thread; all 61 frames exceed the 30 fps budget. P2 is not passed. The measured
shape result is limited to one 8 px CoC point source: broader-radius bokeh,
foreground occlusion, full-frame depth export, optical reference acceptance,
and 4K/120 remain open. Full metrics and hashes are in
`T16-exact-disk-on.run.json`.

The same GPU canary now adds an occlusion case: a far point at depth 100 with
8 px CoC sits behind an opaque green foreground strip at depth 50 with zero
CoC. Readback keeps the foreground `(0,1,0)` without far red contamination
while preserving the far highlight's blur immediately beside the strip
(`R=0.0256952`). The dedicated CTest passes 3/3 cases and 47/47 assertions;
the direct run with the optional PGM capture passes 49/49. This verifies the
native depth-aware surface kernel, not full-scene TextRun depth export or
occlusion. The captured bokeh response remains `T16-point-bokeh-vulkan.png`;
console metrics for all three fixtures are in `T16-exact-disk-canary.log`.

### T16 native depth provenance canary — 2026-11-06

The standalone `chronon3d_vulkan_dof_depth_canary_tests` fixture reads back the
actual Vulkan RGBA32F depth/radius surface from the existing depth-stamp
kernel. With far depth 1700/radius 12 and near depth 700/radius 6, submission
orders far→near and near→far produce identical surfaces. Readback verifies the
overlap pixel resolves to 700/6, far-only coverage remains 1700/12, and
uncovered pixels preserve the −1/−1 sentinel. The printed 4×4 map is in
`T16-vulkan-dof-depth-stamp-test.log`; machine and command provenance are in
`T16-vulkan-dof-depth-stamp.run.json`. CTest passes 1/1; Doctest passes 20/20
assertions. This provides kernel-level depth evidence, not a full-frame depth
export or a scene-level occlusion visual gate.

The broader `chronon3d_backend_registry_tests` CTest target remains FAIL on
this run: 50/134 test cases pass; 84 Vulkan cases throw at `vkCreateInstance`
with `VkResult=-9`, while all 1,906,542 assertions pass. The same result was
reproduced after moving the new canary out of that aggregate target. This
failure is preserved as an open environment/runtime issue and is not reported
as a passing suite.
