# Profiling baseline 2026-10-08 — stage timings

## What was measured

Three full `chronon3d_cli render-plan` runs on the RTX A4000 (Vulkan backend,
output to /tmp), one per calibration shape. The numbers are the `*_ms` fields the
CLI already logs (`[INFO] job-lifecycle:` and `video_diag`), summed across the
three runs. Single run per shape: no repetition statistics.

| Stage (log field) | Total ms (3 runs) | 1080p overlay | 4K overlay | 1080p video (single) |
|---|---:|---:|---:|---:|
| `execute_ms` | 19 950 | 4 093 | 11 699 | (remaining) |
| `wall_ms` | 19 285 | 3 937 | 11 424 | (remaining) |
| `render_ms` | 7 547 | 1 231 | 4 696 | (remaining) |
| `writer_encode_ms` | 6 910 | 1 330 | 5 048 | (remaining) |
| `encode_ms` | 3 970 | 641 | 2 535 | (remaining) |
| `post_sidecar_to_receipt_ms` | 572 | 143 | 225 | (remaining) |
| `session_teardown_ms` | 549 | — | — | — |
| `full_graph_reset_ms` | 535 | — | — | — |
| `dispatch_ms` (14 calls) | 616 | — | — | — |
| `fb_release_ms` (14 calls) | 8 | — | — | — |

Per-shape totals are the three values in each column; the video shape's
per-stage split was not extracted from its log and is part of the 3-run total.

## Top hotspots (ranked by total ms, stage level)

1. `execute_ms` — 19 950 (whole job body; contains 2–5 below)
2. `wall_ms` — 19 285 (end-to-end job wall time)
3. `render_ms` — 7 547 (frame render; GPU work is inside this stage)
4. `writer_encode_ms` — 6 910 (writer, includes encode)
5. `encode_ms` — 3 970 (video encoder)
6. `post_sidecar_to_receipt_ms` — 572 (receipt tail)
7. `session_teardown_ms` — 549
8. `full_graph_reset_ms` — 535 (per-job graph reset; a candidate for caching)
9. `dispatch_ms` — 616 (14 dispatch calls)
10. `full_hash_ms` — 32 (6 calls)

## Limits — read before using these numbers

- **No GPU timestamps.** The CLI does not expose per-pass GPU timestamps; the
  `render_ms` stage is GPU + CPU wall time together. A GPU-side breakdown needs
  Vulkan timestamp queries, which are not wired.
- **No sampling profiler.** The ranking is by logged stage, not by function.
  Nested stages (`execute` ⊃ `render` ⊃ …) are not additive.
- **Single run per shape**, one warm daemon-free process each: variance not measured.
- The 1080p video shape's stage split was not extracted; its total is included
  in the summed columns only.

## Reproduce

```
chronon3d_cli render-plan --input tools/fixtures/vram-calibration-4k-overlay.plan.json \
  --assets-root <Chronon3d> --backend vulkan --output /tmp/prof.mp4 2> /tmp/prof.log
grep -ho "[a-z_]*_ms=[0-9.]*" /tmp/prof.log
```
