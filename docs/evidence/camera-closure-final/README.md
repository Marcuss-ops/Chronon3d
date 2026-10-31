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
| Software vs Vulkan raw | raw runs frame 30/75/149; `raw-parity.json` | PASS frozen aggregate thresholds: MAE 0.211755/0.225626/0.160660; p99 5/4/2. Maximum 122/112/64 and >8 share also recorded; not bit-identical cross-backend |
| Rack-focus raw ROI | `focus-raw.json` | PASS 9/9: focused energy ratio 1.0 at all 3 states; defocused ratios 0.00277–0.16597 |
| Asset alpha | `asset-alpha.json` | PASS asset-local: every card has 36,499 transparent pixels and >562,000 fractional-alpha pixels; not proof of all composited-edge quality |
| Renderer/output validation | `validation-results.json`, `render-*-final.log`, `raw-vulkan-*-final.log` | PASS on completed ON/OFF/raw runs: enabled layers, zero reported errors. Single-job shutter FAIL exit 139 after frame 75; segmented diagnostic delivery in progress |
| Video output/decode/black intervals | `python3 docs/evidence/camera-closure-final/verify_artifacts.py`; `video-metrics.json` | IN PROGRESS pending shutter |
| RenderingGen uploader | RenderingGen `go test ./cmd/drive-upload -count=1`; `drive-upload-tests.log` | PASS |
| Full Optical Studio provider vs offline production render | small typed test plus Motion snapshot tests, NOT full-scene paired render | NOT RUN — no full-fixture equivalence claim |
| Expected-source depth fault injection, native missing-depth end-to-end | unit missing-depth test does not cover all native sources | PARTIAL — not certified by successful normal renders |
| All alpha-edge contamination/occlusion thresholds | raw focus and asset alpha are insufficient to certify every edge | PARTIAL — no universal halo/ghosting claim |
| Human whole-video acceptance | requires named human/reviewer | BLOCKED — owner user/design reviewer, date to agree; agent sentinel inspection is not human approval |
| Clean pair / mandatory CI | `gh auth status` reports no authenticated GitHub host; dirty trees | BLOCKED — owner repository maintainer; date to agree; do not mark same-SHA green |

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
