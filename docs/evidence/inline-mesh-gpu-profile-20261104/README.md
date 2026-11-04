# P2-01 — inline mesh compute profile (2026-11-04)

## Result

**PASS for the requested 10 / 100 / 1,000 / 10,000-triangle GPU profile.**
The same Vulkan compute path rendered every case at 128×96 for 31 frames; all
raw outputs were verified as 31 × 49,152 bytes. Each plan, raw-output hash,
render log, and timing sidecar is kept beside this report. The 1-triangle case
is an extra control. See [the GPU ms and normalized throughput chart](gpu-profile.png)
and [the preview contact sheet](preview-contact.png).

| Triangles | Composite GPU time / job | Composite GPU time / output frame | Nominal full-canvas checks / s |
|---:|---:|---:|---:|
| 1 | 0.576 ms | 0.0186 ms | 0.66 G/s |
| 10 | 0.730 ms | 0.0235 ms | 5.22 G/s |
| 100 | 2.169 ms | 0.0700 ms | 17.56 G/s |
| 1,000 | 16.417 ms | 0.5296 ms | 23.20 G/s |
| 10,000 | 158.887 ms | 5.1254 ms | 23.97 G/s |

Each job rendered 31 frames (5 warm-up, then 26 measured); the per-output-frame
column divides the whole-job Vulkan `composite` pass timestamp by 31. Pass
counts were 99 for each case. The 30 fps frame budget is 33.33 ms, so even the
10,000-triangle low-resolution fixture uses 5.13 ms of composite GPU time per
output frame.

The 10-triangle case is the marginal-cost reference: it has the same projected
mesh bounds as the larger cases. Subtracting its 0.730 ms job total gives
approximately 24.06 billion nominal full-canvas triangle-pixel checks/s for
the 100, 1,000, and 10,000 cases. This count uses the full 128×96 canvas; the
shader dispatch clips to the projected mesh bounds, so it is a normalization
for comparing these runs, not a hardware-independent shader throughput claim.
The first 1-triangle control has a smaller projected bound and is not used for
that marginal slope.

## Decision

Keep the current compute rasterizer for small meshes. The synthetic workload
shows the expected near-linear growth in aggregate composite time beyond 10
triangles, reaching 5.13 ms/frame at 10,000 triangles at 128×96. This profile
does not justify replacing the compute path globally. It does justify the
separate P2-02 A/B on realistic, higher-resolution meshes before choosing a
crossover threshold. P2-02 remains open.

## Fixture and visual check

The plans contain one untextured inline mesh and no external assets. Each
triangle is a broad, repeated triangle in the mesh bounds so the shader's
pixel-by-triangle loop is exercised; the 10-through-10,000 cases have identical
visible output and raw RGBA SHA-256. The one-triangle control has a smaller
projected bound, as expected. The preview images were decoded from the actual
raw Vulkan outputs; the mesh is visible and nontransparent pixels are present.
The source plans are retained under `plans/` and their SHA-256 values are in
`results.json` / `metrics.csv`.

## Environment and command

- Source: clean Chronon3D `main` at `b4c50796c09bbdd0e2d7a86db2305f68d73c4784`.
- Host GPU: NVIDIA RTX A4000, 16 GiB; driver 595.99.02; Vulkan 1.4.329.
- Build: Release, GCC 11.4.0; Vulkan ON; native FFmpeg ON; CUDA interop OFF
  because the remote host has no CUDA toolkit. CLI SHA-256 is in `results.json`.
- Output: Vulkan backend, 128×96, 30 fps, 31 frames, raw RGBA sink.
- Shader: `src/backends/vulkan/shaders/inline_mesh.comp`; its SHA-256 is in
  `results.json`.

The command for each case was:

```sh
chronon3d_cli render-plan \
  --input plans/inline-mesh-<N>.plan.json \
  --output profile-<N>.rgba \
  --assets-root <clean-source-tree> \
  --backend vulkan --video-sink raw \
  --start-frame 0 --end-frame 30
```

GPU milliseconds come from the job-wide `job.gpu.pass_breakdown_ms.composite`
and `job.gpu.pass_counts.composite` fields, populated from Vulkan timestamp
queries. The per-frame `gpu_observed.timestamp_attributed` flag is false in
these raw-sink receipts, so no per-frame GPU median or p95 is claimed. CPU
frame timing fields are retained for context only. `composite` also includes
the fixed non-mesh composite work; the 10-triangle reference subtraction
isolates the growth caused by the larger mesh payload while keeping the output
shape, bounds, and pass count stable.

The remote GPU is shared. An `nvidia-smi` sample was not collected during the
profile runs; a later snapshot showed 0% utilization and unrelated long-lived
daemons, followed by a separate render that started after these runs. The
direct Vulkan timestamp totals are preserved, but no claim of exclusive-host
or cross-run isolation is made.

## Reproduction and artifacts

Run `python3 analyze_profile.py` in this directory to validate the plans,
output frame/byte counts, Vulkan backend identity, pass counts, and sidecars,
then regenerate `results.json`, `metrics.csv`, `gpu-profile.png`,
`gpu-profile.svg`, and `preview-contact.png`.

The raw RGBA sequences remain on the remote run directory; this evidence keeps
their verified SHA-256 values, timing receipts, logs, and representative PNG
frames to avoid checking in 7.5 MiB of redundant raw frames.
