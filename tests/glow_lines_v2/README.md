# Glow Lines V2 documentary render acceptance suite

This is a six-clip, 36-second Chronon3D RenderPlan V3 diagnostic suite. It is
not a claim of CPU/GPU parity. The common output is 1920x1080 H.264, yuv420p,
Rec.709, 30/1 fps; durations are 5, 5, 6, 5, 7, and 8 seconds (1,080 total
frames). Each render is sequential. Temporal motion blur is not enabled in
this acceptance version. A native Vulkan probe segfaulted (exit 139), and the
timing schema has no explicit motion-blur receipt; therefore temporal-blur
validation is explicitly not claimed. A decay clip passes only with at least
12% baseline-corrected reduction in amber-chroma halo signal, measured in a fixed
5-17 px band around the route outside the core. The baseline is the same image at
frame 0 with the route hidden; zero or negative pre-decay excess fails. The
pre sample uses the final full-intensity frame and the post sample is taken after
fade completion; the crisp-core luma must remain measurably above baseline. Clip
04 animates `light.glow.intensity` directly while keeping the emission-layer
opacity at its authored 0.35; layer opacity affects the source before glow
generation and is not a valid emission-fade control.

## Acceptance gates

- The six exact case IDs/durations and V3 canvas/output contract are asserted.
- Drawn routes use a shared linear frame clock for the sharp core and glow
  underlay, with positive and nearly equal arc-length increments.
- Glow is an actual `glow` effect on a second matching path layer, with bounded
  radius/intensity; render receipts must report successful `light.glow`
  software dispatches and no rejected frames.
- Clip 04 animates only `light.glow.intensity` after drawing. The glow-source
  opacity stays at 0.35 and the sharp base stroke stays fully opaque. The MP4 is
  measured against frame 0 in a fixed route halo ROI; baseline-corrected halo
  excess must fall at least 12% between the peak and post-fade samples. A failed render gate writes `status: FAIL` to
  the report and cannot leave behind a stale PASS.
- Editorial layouts reject particles/novelty colors and the full-canvas
  `photo_shade` treatment. Clip 06 uses one photograph, one field-note panel,
  and a connector that spans the image/rail boundary; it may not duplicate the
  evidence photo or place the note outside the rail.
- The aggregate report is PASS only for all six selected cases, not for a
  partial `--case` subset. A render only passes when `ffprobe` confirms H.264,
  1920x1080, 30/1 fps, exact duration/frame count, and yuv420p; start/mid/end frames decode and are
  nonblank; the timing sidecar is valid JSON with finite nonnegative timings;
  the video SHA-256 and byte count are recorded.
- CPU is the software renderer lane. An NVIDIA RTX A4000 is present, but these
  six renders used software processing (`native_frames: 0`); the Vulkan native
  probe exited 139. CPU/GPU parity is therefore `NOT_TESTED`, not a pass. Parity
  requires a successful native render and a measured comparison. Never infer it
  from GPU presence, utilization telemetry, or CPU receipts.

## Commands

From repository root:

```sh
python3 -m unittest discover -s Chronon3d/tests/glow_lines_v2 -p 'test_*.py' -v
python3 Chronon3d/tests/glow_lines_v2/generate_suite.py --generate-only --output-root /tmp/glow-lines-generated
python3 Chronon3d/tests/glow_lines_v2/generate_suite.py --validate-only --output-root /tmp/glow-lines-validated
python3 Chronon3d/tests/glow_lines_v2/generate_suite.py --output-root /tmp/glow-lines-rendered
```

The final command renders cases sequentially with the software backend. It may
be expensive; available measured runs take minutes. The canonical CLI defaults
to `Chronon3d/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli`.
`--cli`, `--case` (repeatable IDs 01–06), and `--output-root` can override the
CLI, selected cases, and output location. The currently verified render lane is
software with hardware encoder disabled; native GPU parity is blocked by an
exit-139 Vulkan single-frame probe and is not part of a software PASS. The report is written as
`<output-root>/report.json`, with each plan/log/frame/timing receipt under its
case directory and each MP4 plus timing JSON at the output root.

## Drive publication

Only publish after the aggregate local report and all six hashes/streams pass.
Publication was not performed. It remains blocked pending externally supplied
Drive credentials, explicit consent to create/use a new destination folder, and
a target folder ID. Do not read or use credentials stored in the repository.
After authorization, use `RenderingGen/bin/drive-upload` with the report's
expected SHA-256 per file; verify actual Drive file IDs, parents, provider
byte-size/checksum, and share URLs. Never put token contents in reports/logs.
Local PASS is not evidence of an upload.
