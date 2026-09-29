# Teardown probe — attributing the intermittent ~1 s (2026-09-28)

Scope: `TICKET-DAEMON-JOB-LIFECYCLE`, open item "un teardown intermittente
~1 s" (`daemon-job-lifecycle-2026-09-16.md` §5, `nvml-sampler-teardown-2026-09-17.md`
§4). This pass does NOT claim the cause: it closes the last un-instrumented gap
so the next probe can name the phase instead of excluding candidates.

## 1. What was already excluded

| Candidate | Evidence that it is not the ~1 s |
|---|---|
| `SystemMetricsCollector` 1 s sleep | fixed; teardown 997.7 → 99.8 ms |
| NVML sampler stop | fixed + regression test; documented as not the cause |
| `encoder_reset_ms` | measured ≈ 1 ms in the intermittent case |
| `full_graph_reset_ms` | measured ≈ 0.1 ms in the intermittent case |
| `slot_pool_close_ms` / `surface_release_ms` | measured ≈ 4 / 10 ms |

## 2. What was NOT instrumented (fixed by this pass)

`~PipeExportSession()` timed five phases and then returned; every remaining
member was destroyed IMPLICITLY after the destructor body, where no timer
reaches. The three with GPU/cross-process state are now reset explicitly and
timed — **in the original implicit order** (member destruction is the reverse of
declaration, so `device_runtime` is released before `device_reservation`):

```
[INFO] job-lifecycle: queue_close_ms=… writer_join_ms=… encoder_reset_ms=…
       decoder_reset_ms=… full_graph_reset_ms=… sink_ctx_reset_ms=…
       device_runtime_reset_ms=… device_reservation_reset_ms=…
```

Why these three:
- `sink_ctx` (`unique_ptr<EncoderSinkContext>`) owns the encoder sink's
  FFmpeg/IO teardown and was previously charged to nothing;
- `device_runtime` (`shared_ptr<media::VideoDeviceRuntime>`) is the last release
  of the GPU runtime — a plausible place for a driver-side stall;
- `device_reservation` is **cross-process GPU state**: releasing it can wait on
  a peer job's lease, which is exactly the shape of an intermittent, job-count-
  independent second.

Remaining tail after these resets is the trivial scalar/string/vector set
(`opts`, `execution_plan`, telemetry vector, …); its cost is bounded by the
CLI's existing caller-tail timer (`command_render_plan_internal`), which already
reports `execute_ms`/`receipt_build_ms` against the sidecar.

## 3. Analyzer

```
Chronon3d/tools/summarize_job_lifecycle.py
```

Parses every `job-lifecycle:` line into `phase → ms`, prints median/p95/max/mean
per phase plus **`spread` = max/median**, and flags records with a selected phase
over a threshold.

`spread` is the signal that matters here: the anomaly is *intermittent*, so the
phase whose peak dwarfs its median owns the second — an absolute threshold alone
would also flag every ordinary job's `execute_ms` (~1000 ms by design), which is
why `--only` exists.

```bash
# one-shot contract check (no GPU required)
python3 tools/summarize_job_lifecycle.py --self-test

# production probe: 20-50 real overlay jobs, teardown phases only,
# non-zero exit when a single phase exceeds 500 ms
journalctl -u renderinggen-worker --since "1 hour ago" \
  | python3 tools/summarize_job_lifecycle.py - \
      --only teardown_ms --only reset_ms --only close_ms --only join_ms \
      --fail-on-outlier
```

Self-test fixture pins the target behaviour: 4 uniform records, one carrying the
intermittent second, must be attributed to `device_runtime_reset_ms` with a
spread ≥ 100× (`self-test OK: … spread 7063x`).

## 4. Acceptance criteria of the probe

1. The analyzer identifies the intermittent phase on ≥ 20 real jobs, or reports
   `no phase over the 500.0 ms threshold` with `spread` ≈ 1 for every phase —
   the second is then NOT in the session teardown and the search moves to the
   caller tail (receipt build/write) and the daemon's job dispatch.
2. If `device_runtime_reset_ms` owns it: the fix is the same shape as the
   metrics-worker fix (make the release non-blocking / bounded, with a
   regression test), measured against the 100-overlay matrix wall.
3. If `device_reservation_reset_ms` owns it: the release is waiting on a peer
   job's GPU lease; the fix belongs in the VPU admission (`gpu_gate`) and must
   be proven with two concurrent daemons, not one.

## 5. First run against production (same day)

Applied to the live warm daemon (`chronon3d.service`, IPC transport), 377 jobs
logged that day:

| phase | jobs | median | p95 | max | spread |
|---|---|---|---|---|---|
| `session_teardown_ms` | 377 | 92.4 | 113.2 | 138.4 | 1.5 |
| `full_graph_reset_ms` | 377 | 62.1 | 77.6 | 96.2 | 1.5 |
| `encoder_reset_ms` | 377 | 28.7 | 42.7 | 66.2 | 2.3 |
| `slot_pool_close_ms` | 377 | 0.0 | 0.0 | 0.0 | 0.0 |
| `queue_close_ms` | 377 | 0.0 | 0.0 | 0.0 | 0.0 |
| `writer_join_ms` | 377 | 0.0 | 0.0 | 0.0 | 0.0 |
| `decoder_reset_ms` | 377 | 0.0 | 0.0 | 0.0 | 0.0 |

`no phase over the 500.0 ms threshold`. Per-job magnitudes on the same corpus:
`execute_ms` median 1574.3 (p95 2416.4, max 3731.8), `receipt_build_ms` median
280.4, `caller_tail_ms` median 280.9.

Reading: **the intermittent ~1 s does not reproduce on the warm-IPC path.** Every
teardown phase is flat (spread ≤ 2.3) across 377 jobs, so the open item is
characteristic of the serialized/CLI path measured on 2026-09-16, not of the
daemon the pipeline uses today. The probe stays in place — and the three newly
timed members stay instrumented — so a regression is attributed on the first day
it appears instead of reopening the candidate list from scratch.
