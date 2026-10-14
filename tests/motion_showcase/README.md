# Chronon3D 20-video motion showcase acceptance suite

This suite is a reproducible **diagnostic lane**, not a claim that all 20 requested
features are complete. The supplied durations sum to **140 seconds** (not 130
approximately), at 30 FPS this is 4,200 total frames when every requested clip
is rendered. Common output is 1920×1080 H.264/yuv420p, 30/1 FPS. The runner
uses synthetic shapes and the repository's Inter font fixture, not the pasted
remote reference images/videos.

## Commands

```sh
python3 Chronon3d/tests/motion_showcase/runner.py --list
python3 Chronon3d/tests/motion_showcase/runner.py --validate-only --output-root /tmp/chronon-validate
python3 Chronon3d/tests/motion_showcase/runner.py --case 01 --render-partial --output-root /tmp/chronon-showcase
python3 Chronon3d/tests/motion_showcase/runner.py --case 10 --render-supported --repeat --output-root /tmp/chronon-showcase
python3 Chronon3d/tests/motion_showcase/runner.py --case 01 --render-partial --backend vulkan --strict-native --output-root /tmp/chronon-native
```

`--cli` can point at another Chronon3D CLI build. `--validate-only` writes each
scene/expected metadata, validates it with the canonical CLI, and produces an
aggregate report. Rendering requires explicit `--case` selection; this prevents
an accidental full-suite run. Rendering is expensive on the available software
consumer: case 01 at five seconds took about 303 seconds and roughly 3.7 GiB
process peak RSS in the observed run. `--strict-native` requires Vulkan plus
`require_gpu_native`, NVENC, a visible GPU, and zero software fallback nodes;
it fails closed and is not a certification command for cases still marked
PARTIAL/BLOCKED. It also checks available RAM before starting GPU rendering.
Artifacts default to this directory's `artifacts/`; keep generated videos out of
source control unless separately requested. `--render-supported` renders cases
marked PASS and PARTIAL, so it is not an all-green functional acceptance command.

Per-case layout is `scene.json`, `expected.json`, `output.mp4`, `probe.json`,
`output.mp4.timing.json`, `frames/frame_000.png`, midpoint, final frame, plus
logs, `metrics.json`, and `report.json`. The report includes source SHA-256,
decoded frame hash summary, ffprobe facts, timing sidecar finite-number checks,
and render wall/RSS observations. `artifacts/validation_report.json` records
canonical validation of all 20 generated plans; validation alone is not render
acceptance. PASS/PARTIAL/BLOCKED/FAIL are deliberately distinct. PARTIAL means
video output may pass the technical transport checks but requested functional
coverage remains incomplete. Synthetic fixture video is never counted as a
functional PASS or visual golden.

## Current verified evidence

- Existing files below `artifacts/` are ignored local outputs from a previous
  run, not current acceptance artifacts. Their reports record manifest SHA-256
  `8fbea36f…`, which does not match the current manifest; the stale aggregate
  report says 20 PASS despite the actual capability boundary. Treat those MP4s,
  metrics, and reports as historical only.
- A fresh validation-only run against the current source wrote to
  `/tmp/chronon-motion-showcase-validation`: all 20 generated plans passed the
  canonical CLI validator. The current suite correctly classifies 10 PARTIAL
  and 10 BLOCKED; the run did not render MP4s. The report manifest hash matches
  the current manifest. Total duration is 140 seconds / 4,200 frames. Plan
  validation alone does not certify feature behavior.
- Case 10 remains PARTIAL because its generated scene does not assert
  independently animated depth-plane/camera-parallax behavior.
- The runner refuses implicit full-suite rendering, checks repeat hashes
  rather than merely recording a mismatch, and fails when required timing
  evidence is absent or invalid. `--strict-native` is a separately selectable,
  fail-closed Vulkan/NVENC route; its output requires native GPU evidence and
  zero software fallback nodes.
- Drive publication requires OAuth files at
  `~/.config/velox/credentials.json` and `~/.config/velox/token.json`. They were
  absent in the inspected environment, so no file upload or Drive folder
  creation has been performed. Once configured, use the `drive-upload` command
  with the requested parent folder ID and a suite-specific `-subfolder`.
- A validation-only run for case 01 also passed after using the repository asset
  root for Inter font resolution.
- Case 10 repeated successfully: both 210-frame renders matched bit-for-bit after
  decode (about 174 seconds including repeat). A repeat attempt for case 01
  exceeded the environment's 600-second command limit after its first full render.
- Representative images were decoded and inspected for dimensions and visible
  non-background content. Semantic feature correctness remains unverified by
  image goldens or per-feature numeric assertions.
- Neither Vulkan nor NVENC strict-native output is certified by this baseline.
  Requests use `--backend software --hardware none`; no GPU fallback is claimed.
  The runner now exposes a separate fail-closed strict-native gate for a future
  GPU render; that gate must not be confused with the software baseline.

## Capability boundaries

The manifest is authoritative for each case's current scope/blockers. Important
end-to-end blockers include SVG morph output; animated rigged/deformed mesh with
texture/alpha transport; temporal rotoscope mattes; feathered animated masks;
verified animated mesh materialization/pixels; video matte composition; and a
single composition integrating those missing paths. ChrononMotion authoring
and its deterministic showcase tests do not by themselves prove consumer pixels.
The separate 60-case Motion showcase checks authoring data and solver contracts,
not these rendered MP4s or pixel goldens.
