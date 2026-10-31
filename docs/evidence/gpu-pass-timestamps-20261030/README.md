# Per-pass GPU timestamps — B2 fixture (2026-10-30)

**Question.** The 100× performance analysis claimed the GPU is not the
bottleneck on the B2 fixture, but the only evidence available was a single
aggregate `gpu_execute_us` counter. This run replaces that aggregate with
exact per-pass-kind device timestamps, so the claim can be checked phase by
phase.

## What was measured

`chronon3d_cli render` on `docs/evidence/100x-perf-waves-b2-20261029/static-text-456f.plan.json`
(1920×1080, 24 fps, 456 frames, color background + animated static title),
Vulkan backend in `require_gpu_native` mode, NVENC native encoder, on an
NVIDIA RTX A4000 (driver 595.99.02).

Command:

```
chronon3d_cli render \
  --plan docs/evidence/100x-perf-waves-b2-20261029/static-text-456f.plan.json \
  --start-frame 0 --end-frame 455 \
  --output docs/evidence/100x-perf-waves-b2-20261029/static-text-456f-pass-timestamps-c-nvenc.mp4 \
  --assets-root <repo> --backend vulkan --gpu-hot-path-mode require_gpu_native \
  --hardware nvenc --encoder-backend native --log-level info
```

Artifacts (in `docs/evidence/100x-perf-waves-b2-20261029/`):

| artifact | value |
| --- | --- |
| result json | `b2-pass-timestamps-c-results.json` |
| run json | `b2-pass-timestamps-c-runs.json` |
| timing sidecar | `static-text-456f-pass-timestamps-c-nvenc.mp4.timing.json` |
| log | `static-text-456f-pass-timestamps-c-nvenc.log` |
| output sha256 | `2520fabea7d548ee04b6f43bd747f9ad64cf1fc7aef1818d01a9f83cb1f7120c` |
| exit code | 0 |

The output hash is identical to the pre-instrumentation B2 run, so the
instrumentation does not change the rendered pixels.

## Result

Job-level GPU time, from `job.gpu` in the sidecar:

| phase | total ms | share | timed regions | median/frame ms | p95/frame ms | max/frame ms |
| --- | --- | --- | --- | --- | --- | --- |
| `composite` | 173.889 | 73.4% | 3192 (7/frame) | 0.3797 | 0.4133 | 0.4232 |
| `layer_batch` | 63.081 | 26.6% | 456 (1/frame) | 0.1337 | 0.1676 | 0.1695 |
| **accounted** | **236.970** | **100.0%** | 3648 | 0.5134 | — | — |

On this execution path `gpu_execute_ms` is now the sum of the same query
spans, so equality with it is expected, not evidence. The independent check is
the per-frame device span (TOP→BOTTOM of the whole submission), whose sum is
**238.985 ms**: the two timed regions cover **99.2% of the device span**, and
the remaining 2.0 ms is untimed clear/sub-pass work. The per-frame device
span median is 0.5128 ms, matching the 0.5134 ms median of the two phases.

Pipeline wall time for the same run: `render_loop_fps = 235.1`,
`end_to_end_fps = 131.2` (≈4.25 ms/frame render loop, ≈7.62 ms/frame
end-to-end). GPU work is therefore ≈0.5 ms of a ≈7.6 ms frame: **12× headroom
on the device side**, confirming that the remaining cost is CPU submission,
encoding and job orchestration — not GPU execution.

`passes_executed` / `gpu_nodes` is 5928 (13 per frame). Of those, 8 per frame
are the timed regions above; the other 5 are passes that do not cross an
`emit_pass_sync` boundary (the transparent clear inside the text batch and
sub-passes recorded within one timed region). They are intentionally **not**
added to a kind, to avoid double counting; they are only visible in the device
span, which is why the coverage check above uses the span and not the kinds.

## How the timestamps are produced

- Two timestamp queries per pass plus two aggregate queries live in a fixed
  per-slot range of one `VkQueryPool` (`2 + 2 × 256` queries per slot).
- `emit_pass_sync` writes the end timestamp of the previous pass and the start
  timestamp of the current pass at the pass boundary, and captures the
  `runtime::GpuPassKind` of the pass at recording time. This happens on both
  execution paths: a compiled `CommandPlan` and the graph path that records
  the same backend passes without a plan.
- The query pair is resolved only after the slot fence signals
  (`read_gpu_timestamps`), so no `VK_QUERY_RESULT_WAIT_BIT` is needed.
- Fence retirement attributes each pair to the originating frame using the
  retained frame index; per-frame values are emitted in
  `frame_times_ms[].gpu_observed.pass_breakdown_ms` / `pass_counts`, job
  totals in `job.gpu.pass_breakdown_ms` / `pass_counts`, and the backend also
  exports them as `gpu_pass_<kind>_us` / `gpu_pass_<kind>_count` counters.
- No timestamps are fabricated: a frame whose queries were not written reports
  empty maps, and `unknown` (reserved kind 10) stays zero.

## Caveats

- Pass kinds are identified from the recording entry point
  (`composite` for fills, inline mesh and composite; `layer_batch` for text and
  layer batches; `blur`/`glow`/`bloom`/`color_adjust`/`matte`/`transform`
  families for filters and transforms). A pass that does not go through
  `emit_pass_sync` (for example the transparent-surface clear in the text
  batch) has no kind of its own and is excluded from the per-kind sums; its
  cost is only visible in the device span (0.004 ms/frame in this run).
- The query range is capped at 256 passes per slot; frames above the cap fall
  back to aggregate timing only (the run above peaks at 8 timed regions).
- One run, single GPU, single machine; the absolute microseconds are not a
  cross-device benchmark, the per-phase ratios are the durable result.

## Addendum — CPU profile of the same fixture (2026-10-30)

The GPU breakdown leaves 3.7 of 4.25 ms/frame on the CPU, so the same run was
profiled with `perf record -F 999 -g` (`perf_event_paranoid` lowered to 1 for
the run and restored to 4 immediately after; CLI at `--log-level info`).

Findings:

- **No application symbol above 0.3% self time.** The loop is many small
  serial operations, not one hot function.
- **25.3% of all samples are `__sched_yield`** inside
  `tbb::detail::r1::thread_dispatcher::process` / `arena::process`: idle TBB
  worker threads spinning on a 24-core host while the loop is mostly serial.
- Shrinking the TBB arena by pinning available cores moves the render loop
  only marginally: 24 cores 238.3 fps, 8 cores **245.8 (+3.2%)**, 4 cores
  242.3, 2 cores 239.9. Even the best case is **18% below** the 300 fps gate.
- The sidecar's per-frame CPU split on a 4.25 ms frame is `preparation`
  2.66 ms, `graph_execute` 0.74 ms, `backend_overhead` 0.61 ms,
  `timeline_eval` 0.26 ms, so preparation/setup is the largest serial phase.

**Decision: no optimization adopted.** The only demonstrated lever is TBB
oversubscription (+3.2% on this host), which cannot change the gate and would
need cross-workload measurement before becoming product behavior. Closing B2
requires the paradigm changes (persistent engine, decoupled encode), not a
micro-fix; the gate stays open and is recorded as such in
`TICKET-100X-PERF-WAVES`.
