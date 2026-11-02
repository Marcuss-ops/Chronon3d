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
| Software vs Vulkan raw | current pair v2, frames 30/75/149; `raw-parity-current-pair-v2.json` | **FAIL / not a DoF parity measurement**: RGB MAE 28.99/27.59/27.12, p99 75/83/83. Isolated software background output is clear-color only while Vulkan displays the plate; diagnose the camera-projected image source path before using this scene for backend comparison. Frozen limits (MAE <=2, p99 <=10) are unchanged. |
| Rack-focus raw ROI | `strict-native-current-pair-v2-report.json`; raw readbacks `dof-{30,75,149}.rgba` and `sharp-{30,75,149}.rgba` | PASS 9/9 with deterministic center line-pair charts, unchanged ROIs and thresholds: focused ratios 1.0, 1.0, 0.9987; defocused ratios <=0.5807. This certifies raw Vulkan focus response only, not alpha-edge occlusion or CPU/Vulkan parity. |
| Asset alpha | `asset-alpha.json` | PASS asset-local: every card has 36,499 transparent pixels and >562,000 fractional-alpha pixels; not proof of all composited-edge quality |
| Renderer/output validation | v2 strict-native ON/OFF logs and timing sidecars in this directory | PASS for serialized single-flight ON and OFF 150-frame runs: enabled validation layers, zero reported errors. This does not certify asynchronous Vulkan resource retirement. |
| Video output/decode/black intervals | `strict-native-current-pair-v2-report.json`, v2 ON/OFF MP4s | PASS: both strict-native 150-frame 1920×1080/30 outputs decode, have no detected black intervals, and exit successfully. Shutter accumulation and asynchronous surface lifetime remain separate gates. |
| RenderingGen uploader | RenderingGen `go test ./cmd/drive-upload -count=1`; `drive-upload-tests.log` | PASS |
| Full Optical Studio provider vs offline production render | small typed test plus Motion snapshot tests, NOT full-scene paired render | NOT RUN — no full-fixture equivalence claim |
| Expected-source depth fault injection, native missing-depth end-to-end | unit missing-depth test does not cover all native sources | PARTIAL — not certified by successful normal renders |
| All alpha-edge contamination/occlusion thresholds | raw focus and asset alpha are insufficient to certify every edge | PARTIAL — no universal halo/ghosting claim |
| Human whole-video acceptance | requires named human/reviewer | BLOCKED — owner user/design reviewer, date to agree; agent sentinel inspection is not human approval |
| Clean pair / mandatory CI | `gh auth status` reports no authenticated GitHub host; dirty trees | BLOCKED — owner repository maintainer; date to agree; do not mark same-SHA green |

### 2026-11-01 current-pair 1080p rerun

The current Motion working tree generated plan SHA `15f650b4ed70a05401ff42385246493e60733a3054bc33f8b1ad84229876decd`. Strict-native Vulkan/NVENC DoF ON and sharp-control OFF each completed 150 frames at 1920×1080/30, H.264, exit 0, with successful decode, no detected black intervals, and zero Vulkan validation errors. Both require `CHRONON3D_VULKAN_SINGLE_FLIGHT=1`; this proves the serialized route only, not asynchronous surface lifetime. Video hashes, logs and timing sidecars are retained as `optical_current_pair_v2_*`.

The test graphics now place high-contrast line-pair charts in the measured card ROIs. Raw Vulkan readbacks pass all 9 preregistered focus assertions with the original ROIs and limits. DoF-off/on lossy MP4 measurements are retained as diagnostics; raw RGBA readback is the focus oracle. Motion provider and offline emitters produced the same plan SHA and matching 1,350 camera samples plus layer transforms. However, raw software/Vulkan comparison **fails** the frozen limits: MAE 28.99/27.59/27.12, p99 75/83/83. A separate software run with the CLI `--diagnostic` layout-preview flag produced clear-color output and is excluded from renderer diagnosis; a clean shuttered software frame was stopped after exceeding 80 seconds and 2.4 GB RSS. Thus the comparison gate remains FAIL, but the exact software-side cause is NOT ESTABLISHED. Use a focused one-sample fixture for diagnosis, then rerun the identical frozen full-frame oracle; do not alter its thresholds. Details: `raw-parity-current-pair-v2.json`, `strict-native-current-pair-v2-report.json`, and `motion-provider-offline-parity.json`.

### 2026-11-01 current-pair v3 fixture-size repair

The background plane was 8400×4725 world units for a 50° camera at 2000-unit distance, despite the asset itself being 1920×1080. Motion now authors a 3840×2160 plate (about 16% overscan of the visible field) to cover the image without the oversized software producer allocation. The v3 offline and provider plans are byte-identical, SHA-256 `c2467b44fed0f0d007af7b3e38a3dd57e13728295b506701d7d3c02306e376d6`, and preserve all 1,350 synchronized camera/layer samples. Plan validation passes at 1920×1080/30 for 150 frames.

A static one-frame cover probe (DoF/shutter disabled only in the temporary probe) compares software and Vulkan at RGB MAE `1.0374`, p99 `5` under the frozen limits 2/10; Vulkan validation errors: 0. This proves parity for the synthetic still probe, not the animated acceptance pair. The v3 raw Vulkan rack-focus gate passes 9/9 with the unchanged ROIs and thresholds: near@30 `0.9443`, middle@75 `0.8856`, far@149 `0.9956`; all out-of-focus ROI ratios are <=`0.90`. Full strict-native Vulkan/NVENC 150-frame DoF-on acceptance is running as `optical_current_pair_v3_dof_1080p.*`; sharp-control full acceptance and software/Vulkan animated parity are still pending. Machine-readable focus measurements and raw hashes are in `strict-native-current-pair-v3-focus-report.json`.

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
