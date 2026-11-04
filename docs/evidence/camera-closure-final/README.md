# Camera closure — acceptance run 2026-10-30

## Goal and identity

Complete the existing [camera closeout §8](../../tickets/TICKET-CAMERA-DEFINITIVE-CLOSURE.md), not universal After Effects equivalence. Motion owns authored evaluation; Chronon3d owns projection, compositing, depth and supported DoF. Functional expansion and typed-video demolition are tracked separately in [AE camera functional parity](../../tickets/TICKET-AE-CAMERA-FUNCTIONAL-PARITY.md).

Initial/source HEAD pair: Motion `b778afc8b21ec61c7d2d5152319ca7bea8d01872`, C3D `9b5d23833ee48c1b5b8e0a3ca76b90303eb10825`, BOTH WITH DIRTY CHANGES. See `revisions.json` for binary and tracked-diff digests; these do not replace a commit including untracked headers. No commit/push or same-SHA CI certification is implied. Pre-existing unrelated Vulkan/performance and Template work is preserved.

Host: Linux, NVIDIA RTX A4000 16 GB, driver 595.99.02. Builds use ccache/mold and `-j16`, one heavy compiler build at a time. Existing daemon processes are untouched. Sources/assets and font are repository-local; font is `ChrononMotion3D/render/spike/assets/t19b_apollo/Inter-Bold.ttf`.

## Scene contract

Original Optical Studio: generated calibration graphics only, no celebrities/crowd/photos/mesh substitution. Perspective camera parented to a moving Null, four image planes (background plus near/middle/far), alpha foreground translating/fading in, horizontal camera dolly/parallax, focus 820 → 1000 → 1380 scene units. 1920×1080, 30 fps, 150 frames. Variants: DoF ON, identical DoF OFF, DoF + 180°/8 shutter. C3D receives resolved Motion state; no renderer-side keyframe edits.

Preview: `preview.mp4`/`preview-075.png`, 640×360 strict-native Vulkan with validation. Final sentinel PNGs are decoded from MP4; raw sink files are headerless RGBA8 despite `.mp4` names (video routing requires the extension). Never feed those raw files to ffprobe as containers.

## Frozen measurements

`measurement-contract.json` fixes frame/ROI coordinates, raw backend MAE <=2 and p99 <=10 on 8-bit RGB, and focus ON/OFF edge-energy thresholds BEFORE rendering the corrected fixture. Focused plane retains >=0.70 of OFF energy, defocused planes <=0.90 at near@30, middle@75, far@149. `assets.sha256` identifies generated plates, generator and font; `asset-alpha.json` records transparent/fractional pixels. No threshold widening.

## Gate table (local scope)

All rows use the dirty HEAD pair and host above; commands are relative to C3D unless stated. PASS is limited to the explicit row, not the entire §8 compound requirement.

| Gate | Command / evidence | Result |
|---|---|---|
| Shared camera admission, resolved transforms, typed provider/offline raw, seek, wrap, image clipping | `build/chronon/linux-video-test/tests/chronon3d_camera_tests`; `camera-suite-final.log` | PASS: 82 cases, 66,164 assertions (before the later focused DoF-copy repair; camera sources unchanged) |
| Motion authoring/F3, camera bake, JSON and boundary | Motion `ctest --test-dir build/release-fast -R '^(chrononmotion_authoring\|chrononmotion_camera_bake_roundtrip\|chrononmotion_renderplan_camera_parity)$' --output-on-failure`; `motion-tests-final.log` | PASS: 3/3 after precision repair |
| Camera architecture | `python3 tools/check_architecture.py --root . --rule-prefix camera`; `camera-architecture.log` | PASS: 6/6 |
| Missing/malformed tracked CPU depth and unsupported lens shaping | `chronon3d_render_graph_tests --test-case='*DOF rejects missing*,*PR2-Unit-DoF*'`; `dof-node-tests.log` | PASS: 5 cases, 18 assertions after DoF-copy repair; remaining 435 cases excluded by focused filter, not run |
| Software vs Vulkan raw | current pair v3, frames 30/75/149; `parity-v3-raw-sentinels.json` | **PASS**: raw RGBA8 readback vs software RGBA8 under frozen limits (MAE <=2, p99 <=10). Frame 30 MAE 0.1740, p99 1; Frame 75 MAE 0.1772, p99 2; Frame 149 MAE 0.1523, p99 1. Vulkan validation errors: 0. |
| Rack-focus raw ROI | `strict-native-current-pair-v2-report.json`; raw readbacks `dof-{30,75,149}.rgba` and `sharp-{30,75,149}.rgba` | PASS 9/9 with deterministic center line-pair charts, unchanged ROIs and thresholds: focused ratios 1.0, 1.0, 0.9987; defocused ratios <=0.5807. This certifies raw Vulkan focus response only, not alpha-edge occlusion or CPU/Vulkan parity. |
| Asset alpha | `asset-alpha.json` | PASS asset-local: every card has 36,499 transparent pixels and >562,000 fractional-alpha pixels; not proof of all composited-edge quality |
| Renderer/output validation | v2 strict-native ON/OFF logs and timing sidecars in this directory | PASS for serialized single-flight ON and OFF 150-frame runs: enabled validation layers, zero reported errors. This does not certify asynchronous Vulkan resource retirement. |
| Video output/decode/black intervals | `strict-native-current-pair-v2-report.json`, v2 ON/OFF MP4s | PASS: both strict-native 150-frame 1920×1080/30 outputs decode, have no detected black intervals, and exit successfully. Shutter accumulation and asynchronous surface lifetime remain separate gates. |
| RenderingGen uploader | RenderingGen `go test ./cmd/drive-upload -count=1`; `drive-upload-tests.log` | PASS |
| Full Optical Studio provider vs offline production render | `chronon3d_camera_tests --test-case='*Optical Studio*'`; `motion-provider-full-scene-parity.json` | **PASS**: offline program (1,350 baked samples) and live `RuntimeCameraSampleProvider` callback dynamically evaluated across the calendar produce identical compiled program fingerprints, node counts, layer counts, pass counts, and exact matching parameter blocks across sentinels 30, 75, 149; conflict and empty-provider fail-closed checks PASS. |
| Expected-source depth fault injection, native missing-depth end-to-end | unit missing-depth test does not cover all native sources | PARTIAL — not certified by successful normal renders |
| All alpha-edge contamination/occlusion thresholds | raw focus and asset alpha are insufficient to certify every edge | PARTIAL — no universal halo/ghosting claim |
| Human whole-video acceptance | requires named human/reviewer | BLOCKED — owner user/design reviewer, date to agree; agent sentinel inspection is not human approval |
| Clean pair / mandatory CI | `gh auth status` reports no authenticated GitHub host; dirty trees | BLOCKED — owner repository maintainer; date to agree; do not mark same-SHA green |

### 2026-11-01 current-pair 1080p rerun

The current Motion working tree generated plan SHA `15f650b4ed70a05401ff42385246493e60733a3054bc33f8b1ad84229876decd`. Strict-native Vulkan/NVENC DoF ON and sharp-control OFF each completed 150 frames at 1920×1080/30, H.264, exit 0, with successful decode, no detected black intervals, and zero Vulkan validation errors. Both require `CHRONON3D_VULKAN_SINGLE_FLIGHT=1`; this proves the serialized route only, not asynchronous surface lifetime. Video hashes, logs and timing sidecars are retained as `optical_current_pair_v2_*`.

The test graphics now place high-contrast line-pair charts in the measured card ROIs. Raw Vulkan readbacks pass all 9 preregistered focus assertions with the original ROIs and limits. DoF-off/on lossy MP4 measurements are retained as diagnostics; raw RGBA readback is the focus oracle. Motion provider and offline emitters produced the same plan SHA and matching 1,350 camera samples plus layer transforms. However, raw software/Vulkan comparison **fails** the frozen limits: MAE 28.99/27.59/27.12, p99 75/83/83. A separate software run with the CLI `--diagnostic` layout-preview flag produced clear-color output and is excluded from renderer diagnosis; a clean shuttered software frame was stopped after exceeding 80 seconds and 2.4 GB RSS. Thus the comparison gate remains FAIL, but the exact software-side cause is NOT ESTABLISHED. Use a focused one-sample fixture for diagnosis, then rerun the identical frozen full-frame oracle; do not alter its thresholds. Details: `raw-parity-current-pair-v2.json`, `strict-native-current-pair-v2-report.json`, and `motion-provider-offline-parity.json`.

### 2026-11-01 current-pair v3 fixture-size repair

The background plane was 8400×4725 world units for a 50° camera at 2000-unit distance, despite the asset itself being 1920×1080. Motion now authors a 3840×2160 plate (about 16% overscan of the visible field) to cover the image without the oversized software producer allocation. The v3 offline and provider plans are byte-identical, SHA-256 `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`, and preserve all 1,350 synchronized camera/layer samples. Plan validation passes at 1920×1080/30 for 150 frames.

A static one-frame cover probe (DoF/shutter disabled only in the temporary probe) compares software and Vulkan at RGB MAE `1.0374`, p99 `5` under the frozen limits 2/10; Vulkan validation errors: 0. This proves parity for the synthetic still probe, not the animated acceptance pair. The v3 raw Vulkan rack-focus gate passes 9/9 with the unchanged ROIs and thresholds: near@30 `0.9443`, middle@75 `0.8856`, far@149 `0.9956`; all out-of-focus ROI ratios are <=`0.90`. Machine-readable focus measurements and raw hashes are in `strict-native-current-pair-v3-focus-report.json`.

### 2026-11-02 current-pair v3 strict-native 1080p checkpoint

Both full Vulkan/NVENC v3 outputs completed locally without `CHRONON3D_VULKAN_SINGLE_FLIGHT`: DoF/shutter ON (`optical_current_pair_v3_dof_nosf_1080p.mp4`, SHA-256 `c78cfaf096e368b72d32a1a44237e367427e5618a64e434174b4617ab85ac3d6`) and sharp-control OFF (`optical_current_pair_v3_sharp_nosf_1080p.mp4`, SHA-256 `a5101c29c80197b07d065e7c2bc85631088e1e4d84aa0e15cfcb316e67f9dac6`). Each is H.264 1920×1080/30, 150/150 frames, NVENC accepted all frames, copy-eligible, decodes cleanly, has no detected black interval, and records zero Vulkan validation errors/VUIDs. Logs and timing sidecars share the `optical_current_pair_v3_{dof,sharp}_nosf_1080p` basename.

This supersedes the earlier statement that the full v3 1080p render was running or that only 320×180 had passed. The raw Vulkan focus report remains PASS 9/9. Decoded-video focus checks are diagnostic only. The earlier `v3-parity-*` raw-sink probes produced black frames and are invalid for backend parity; do not use them. Fresh strict-native Vulkan H.264 NVENC QP0 (`lossless` preset) sentinels compared with fresh software RGBA raw give: frame 30 MAE `1.8158`, p99 `11` (**FAIL**); frame 75 MAE `1.4539`, p99 `9` (PASS); frame 149 MAE `1.5042`, p99 `9` (PASS). Vulkan validation errors: zero for each sentinel. Reproduce metrics with `python3 docs/evidence/camera-closure-final/measure_parity_v3_lossless_sentinels.py`; machine-readable hashes/results are in `parity-v3-lossless-sentinels.json`. In the preregistered ROI diagnostics, near-plane p99 is `2/3/6`, middle `11/10/11`, far `6/6/2` at frames `30/75/149`. The near ROI overlaps the foreground-alpha plane, but does not isolate its edge band, so this is not an anti-halo PASS. This is an encoder-normalized diagnostic, not the frozen raw RGBA comparison: the native H.264 stream is yuv420p/BT.709 limited range even at QP0. Targeted graph tests pass alpha fractional compositing, layer-order occlusion and missing native depth fail-closed: `chronon3d_render_graph_tests` 5/5 cases, 44/44 assertions; log `camera-alpha-depth-targeted-20261102.log`. C3D runtime-provider pixel/shutter API tests pass 2/2 cases, 105/105 assertions; Motion `chrononmotion_camera_bake_roundtrip` passes (log `camera-motion-provider-targeted-20261102.log`). The v3 offline and provider-generated shutter RenderPlans are byte-identical, SHA `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`; these certify provider sampling/materialization and the C3D provider API independently, not injection of the live Motion callback into C3D's full render. Native GPU alpha-edge/fault-injection evidence remains partial. Human whole-video review and clean same-SHA CI also remain open. Worktrees were dirty during these local runs; no commit, CI or human sign-off is implied.

### 2026-11-02 raw software vs Vulkan parity PASS under frozen thresholds

The Vulkan `--video-sink raw` host download path was restored by marking GPU surfaces authoritative at the source and preventing uninitialized host pool drops in `framebuffer_acquire_owned_detail.hpp`. A graph pass anomaly that inserted a duplicate post-composite `PerPixelDofNode` on Vulkan was resolved by checking native surface support in `OutputPass`. In `layer_dof_native.cpp`, native layer DoF was aligned with software's separable Gaussian kernel (`dof_blur_surface`) matching the exact math `exp(-4.5 * t^2)`.

Official raw RGBA8 sentinel comparison between Software and Vulkan (with validation enabled) on the frozen v3 pair (`c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`):
- **Frame 030:** RGB MAE `0.1740`, p99 `1`, max `5` (**PASS**)
- **Frame 075:** RGB MAE `0.1772`, p99 `2`, max `27` (**PASS**)
- **Frame 149:** RGB MAE `0.1523`, p99 `1`, max `2` (**PASS**)
- **Vulkan Validation Errors:** 0 on all frames.
- **Aggregate Gate:** **PASS** (reproduced via `python3 docs/evidence/camera-closure-final/measure_parity_v3_raw_sentinels.py`; artifact `parity-v3-raw-sentinels.json`).

### 2026-11-02 acceptance video renders & Google Drive delivery

Fresh acceptance videos rendered with the latest math alignment and duplicate-pass removal fixes, uploaded to Google Drive folder `1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS` via `RenderingGen/renderinggen/bin/drive-upload`:

1. **Optical Studio v3 — Sharp Control (1080p / 30fps / 150 frames):**
   - **File:** `optical_v3_sharp_control_1080p.mp4` (1,716,424 bytes)
   - **SHA-256:** `a5101c29c80197b07d065e7c2bc85631088e1e4d84aa0e15cfcb316e67f9dac6`
   - **Drive ID:** `1vmUi3Z7ZDeftWbUXNQuTh5-o7BKSdYJT`
   - **Drive URL:** [optical_v3_sharp_control_1080p.mp4](https://drive.google.com/file/d/1vmUi3Z7ZDeftWbUXNQuTh5-o7BKSdYJT/view?usp=drivesdk)
   - **Validation:** 150/150 frames, NVENC accepted all frames, copy-eligible, zero Vulkan validation errors.

2. **Optical Studio v3 — DoF + Shutter Rack Focus (1080p / 30fps / 150 frames):**
   - **File:** `optical_v3_dof_shutter_1080p.mp4` (1,156,189 bytes)
   - **SHA-256:** `86c5ddf805db5d2477cfc7d3dfc038588083b481afddc77ee39fcba378208f33`
   - **Drive ID:** `1SJuU4S26hwxfR1d8PbQus2rCxpqgC0gr`
   - **Drive URL:** [optical_v3_dof_shutter_1080p.mp4](https://drive.google.com/file/d/1SJuU4S26hwxfR1d8PbQus2rCxpqgC0gr/view?usp=drivesdk)
   - **Validation:** 150/150 frames (1,350 sub-passes), NVENC accepted all frames, copy-eligible, zero Vulkan validation errors.

3. **Comparison Side-by-Side Full-Res (3840×1080 / 30fps / 150 frames):**
   - **File:** `optical_v3_side_by_side_3840x1080.mp4` (1,220,276 bytes)
   - **SHA-256:** `9aa18c49e356c14792db243681821ef032a03a1b206443e9858a7651e0121d76`
   - **Drive ID:** `1AE2ANu1CkR5ZIdiVFvyKX_DouMTyHTxb`
   - **Drive URL:** [optical_v3_side_by_side_3840x1080.mp4](https://drive.google.com/file/d/1AE2ANu1CkR5ZIdiVFvyKX_DouMTyHTxb/view?usp=drivesdk)
   - **Content:** Unscaled side-by-side comparison showing Sharp Control on the left and Optical Studio v3 (DoF + Shutter) on the right with on-screen labels.

4. **Comparison Side-by-Side Standard 16:9 (1920×1080 / 30fps / 150 frames):**
   - **File:** `optical_v3_side_by_side_1080p.mp4` (359,605 bytes)
   - **SHA-256:** `fecd3a3ba81954449a9b779803cc4fc0f76be5e8bd65cbd21e22b6119dbd4d3a`
   - **Drive ID:** `1NO1VfMN7-uLu2DwbYPt-2uSpbsyIxhTg`
   - **Drive URL:** [optical_v3_side_by_side_1080p.mp4](https://drive.google.com/file/d/1NO1VfMN7-uLu2DwbYPt-2uSpbsyIxhTg/view?usp=drivesdk)
   - **Content:** Letterboxed 16:9 side-by-side layout for standard video players and mobile viewing.

## Defects reproduced and repaired

1. Layer `transform_samples.frame` used `Writer::number(double)`, narrowing to float; 24/827 Motion checks failed. Reuse existing `exactNumber(double)` (as camera timestamps already do). Rebuilt Motion and reran 3/3 tests PASS without tolerance changes.
2. DoF + shutter strict-native failed at frame 0: `CUDA encoder destination must be exportable ...`. The per-sample terminal is ordinary Vulkan storage, not a CUDA encoder export. `PerPixelDofNode` now checks canonical `encoder_compatible` residency: true encoder destinations use video-copy; intermediates use auxiliary Vulkan copy with conservative barriers. Focused DoF tests PASS and two independent raw shutter frame-75 runs are bit-identical (`shutter-repeat.sha256`). No CPU fallback introduced. HOWEVER the full 150-frame validation-enabled shutter job subsequently exited 139 after frame 75, with no validation error logged: full-job stability remains FAIL. Six 25-frame native segments are attempted as a delivery workaround, not a fix or a full-job PASS.
3. Original pre-projection image clipping fix and oversized-background regression were already in the working tree; camera suite passes. The Optical Studio background now covers the viewport. Foreground entry starts at -540 instead of -700 scene units to keep initial framing inside the image.

## Reproduction commands

From workspace root:

```bash
python3 ChrononMotion3D/tools/generate_optical_studio_assets.py \
  Chronon3d/docs/evidence/camera-closure-final/assets \
  ChrononMotion3D/render/spike/assets/t19b_apollo/Inter-Bold.ttf
for mode in dof sharp shutter; do
  ChrononMotion3D/build/release-fast/chrononmotion_plan_optical_studio \
    Chronon3d/docs/evidence/camera-closure-final/optical_${mode}.plan.json \
    docs/evidence/camera-closure-final/assets 1920 "$mode"
done
```

From C3D:

```bash
cmake --build build/chronon/linux-video-release --target chronon3d_cli -j16
for mode in dof sharp shutter; do
  CHRONON3D_VULKAN_VALIDATION=1 \
  build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli render \
    --plan docs/evidence/camera-closure-final/optical_${mode}.plan.json \
    --assets-root "$PWD" --backend vulkan --gpu-hot-path-mode require_gpu_native \
    --hardware nvenc --encoder-backend native \
    -o docs/evidence/camera-closure-final/optical_${mode}_1080p.mp4
done
# Raw oracle: replace output name and add these flags (no native-encode requirement):
# --start-frame 75 --end-frame 75 --video-sink raw --pipe-pixfmt rgba --encoder-backend pipe
# Repeat for backend software and vulkan, frame 30/75/149.
python3 docs/evidence/camera-closure-final/verify_artifacts.py
```

Upload from RenderingGen/renderinggen using existing credentials, never their contents:

```bash
go run ./cmd/drive-upload -credentials ../UploadDrive/credentials.json \
  -token ../UploadDrive/token.json -folder 1J_xUGo_bchzXDIGqSX04CU44c_Dm3SxS \
  -file ../../Chronon3d/docs/evidence/camera-closure-final/optical_dof_1080p.mp4 \
  -sha256 "$(sha256sum ../../Chronon3d/docs/evidence/camera-closure-final/optical_dof_1080p.mp4 | cut -d' ' -f1)"
```

## Drive deliveries

- DoF rack focus: [open](https://drive.google.com/file/d/1bh5iQY4rPdK2Hb_8476My6yfAE5kP4Nf/view?usp=drivesdk), 1,680,154 bytes, SHA-256 `a5a3501d74ddde99225de8a5f677f5a6e51938498be7882b3027805c42d20c57`.
- DoF OFF: [open](https://drive.google.com/file/d/1OwpRSaHL9gc_peTdOTeajUueSC4APz_L/view?usp=drivesdk), 2,721,145 bytes, SHA-256 `58aab9d466cd7d3ca7d1b324fe9ef5eb49d22584b84100d8ca19904c542f0371`.
- Shutter: pending completion.

Both completed uploads report `DRIVE_UPLOAD_PASS` with the requested parent folder, size and pinned local SHA-256. Publisher verifies provider MD5 and byte accounting; Drive does not return an independent SHA-256. Final-binary DoF/OFF rerenders match the uploaded SHA-256 byte-for-byte.

## Closure decision

**PARTIAL, not closed.** Mandatory clean same-SHA CI, named human full-video review and the unfulfilled compound §8 gates above cannot be inferred from local tests, a demo or an upload. A future expansion must use its own acceptance rather than claim all AE parity from these results.

### 2026-11-02 raw Vulkan readback recheck and definitive resolution

Repeated the preregistered frame-30 raw command on the rebuilt CLI with
validation enabled. The process completed 1/1 frames and wrote exactly
8,294,400 RGBA bytes, but every RGB channel was zero (mean 0, max 0, zero
nonzero channels). Artifact SHA-256:
`788ae0147bdf979a6575938ca2d7d4403788588f7be2010f03776c968fd1ab49` at
`parity-v3-vulkan-030-raw-recheck.raw.mp4`.

**Root cause and definitive resolution:** The host readback drop was caused by
`framebuffer_acquire_owned_detail.hpp` copying uninitialized host CPU memory from
the pool when `other.data()` was non-null, discarding the native GPU surface, and
`source_node_execute.cpp` lacking `mark_gpu_authoritative()`. Additionally,
`graph_builder_output_pass.cpp` was injecting a redundant post-composite
`PerPixelDofNode` on Vulkan, and `layer_dof_native.cpp` was calling
`dof_blur_disk_surface` instead of the 2-pass separable Gaussian blur
`dof_blur_surface` (`exp(-4.5 * t^2)`).
Once resolved, the official raw RGBA8 sentinels passed all frozen thresholds
with zero validation errors (`parity-v3-raw-sentinels.json`).

### 2026-11-04 current-pair C6 raw rereun

On the current C3D source mirror (base HEAD
`b1b21cbed1d8927de8929a33924c1f4d097955c4`, compiled-source worktree-diff
SHA-256 `02d5da0c988c1e6e0bdb5406ae57dbafd9b173c5d748e04465a608691fa55318`),
the current pair v3 was rendered on the A4000 with the Vulkan backend and
validation layer. These two raw sink runs intentionally use host readback;
they prove the pre-encode optical image and do not prove strict-native video.
Both runs were 1920×1080, 30 fps, frames 0–149, without single-flight.

- DoF/shutter ON, plan SHA-256
  `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`:
  150 frames, exact 1,244,160,000 RGBA8 bytes, raw SHA-256
  `472f37621c2a5fed356e29beb8644482480aa6dfec9d3b6efa4cfb6be8d46b03`;
  no black frames, 150 distinct frame hashes, zero validation/VUID matches.
- Sharp OFF, plan SHA-256
  `1b26dd00bdc0bec1f34ccba7504296d650715df7404bacf56aba0ce375ef4c61`:
  same frame count and byte count, raw SHA-256
  `70199e5995c00cb99a5c9c4569fe1ba5396dd2789e5be0b47fb57139244e6448`;
  no black frames, 150 distinct frame hashes, zero validation/VUID matches.
- The frozen focus ROIs pass all nine ratios: near@30 `0.95284`, middle@75
  `0.89055`, far@149 `0.99958`; all defocused ROIs are at or below `0.90`.
  See [raw focus report](c6-v3-raw-focus-report.json), [DoF sequence report](c6-v3-dof-raw-report.json),
  [sharp sequence report](c6-v3-sharp-raw-report.json), and
  [verification scripts](verify_raw_sequence.py) / [focus metrics](measure_raw_focus_pair.py).
  Preview frames: [DoF near](c6-dof-030.png), [DoF middle](c6-dof-075.png),
  [DoF far](c6-dof-149.png), [sharp near](sharp-030.png),
  [sharp middle](sharp-075.png), [sharp far](sharp-149.png).

The current-source CUDA-enabled release CLI was built on the GPU host with
`ninja -j12 chronon3d_cli` (683/683, exit 0), binary SHA-256
`f22f9b5e1d3cc02bf4deea5c7c89ca9f3aff6a2a85ca5e7c639b7b61ed679a94`.
The raw readback pair above used the earlier local fastdev CLI SHA-256
`f3f4010eb13e19e6d5c9bbf440fe7dabb6cbf07d7f090fd3e643cfd18a31486c`.

The full current-source 150-frame strict-native DoF/shutter run has now
completed on the A4000: Vulkan `require_gpu_native`, native `h264_nvenc`,
validation layers enabled, and single-flight disabled. It exited 0, accepted
150 frames and muxed 150 packets with PTS 0–149. The resulting H.264 video is
1920×1080/30, decodes as 150 frames, and blackdetect found no black intervals.
There are zero `Vulkan Validation ERROR`, `Validation Error`, or `VUID-`
matches in the run log. Video SHA-256 is
`1c6e43b19c00bbf175b2204341ef6f71de2186ac4f10d8a1e11c92fa5d00cc83`.
See the [native verification report](c6-v3-native-verification.json),
[video](c6-v3-native.mp4), [timing sidecar](c6-v3-native-timing.json), and
[run log](c6-v3-native.log). This closes the local strict-native C6/P0-13
run subgate, not the overall ticket: C7 human review and same-SHA CI remain
open.
