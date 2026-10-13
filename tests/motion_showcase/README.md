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
python3 Chronon3d/tests/motion_showcase/runner.py --validate-only
python3 Chronon3d/tests/motion_showcase/runner.py --case 01 --render-partial --output-root /tmp/chronon-showcase
python3 Chronon3d/tests/motion_showcase/runner.py --render-supported --output-root /tmp/chronon-showcase
python3 Chronon3d/tests/motion_showcase/runner.py --case 10 --render-supported --repeat --output-root /tmp/chronon-showcase
```

`--cli` can point at another Chronon3D CLI build. `--validate-only` writes each
scene/expected metadata, validates it with the canonical CLI, and produces an
aggregate report. Rendering is expensive on the available software consumer:
case 01 at five seconds took about 303 seconds and roughly 3.7 GiB process peak
RSS in the observed run. Run selected cases rather than an unbounded full suite.
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

- Canonical software renders for cases 01 and 10 are saved with extracted
  representative frames and timing sidecars under `artifacts/`. Both are H.264,
  yuv420p, 1920×1080, 30/1 FPS, with exact 150/5s and 210/7s output. Case 01
  had 94 distinct decoded frames and 11,636 finite timing metrics; case 10 had
  73 distinct decoded frames, 16,136 finite timing metrics, and exact repeat-run
  decoded hash equality. Both remain partial/not functionally certified.
- Canonical `validate --plan` passed for all 20 generated plans; the checked-in
  artifact summary is `artifacts/validation_report.json`. The aggregate evidence
  is `artifacts/report.json`. The summed duration is 140 seconds / 4,200 frames.
  The manifest has 9 BLOCKED and 11 PARTIAL cases; validation does not imply
  those capabilities are implemented. Case 10 is PARTIAL because its generated
  scene still lacks independently animated depth-plane/camera-parallax assertions.
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

## Capability boundaries

The manifest is authoritative for each case's current scope/blockers. Important
end-to-end blockers include SVG morph output; animated rigged/deformed mesh with
texture/alpha transport; temporal rotoscope mattes; feathered animated masks;
verified animated mesh materialization/pixels; video matte composition; and a
single composition integrating those missing paths. ChrononMotion authoring
and its deterministic showcase tests do not by themselves prove consumer pixels.
The separate 60-case Motion showcase checks authoring data and solver contracts,
not these rendered MP4s or pixel goldens.
