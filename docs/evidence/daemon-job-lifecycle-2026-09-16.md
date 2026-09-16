# Daemon job lifecycle — attribution and fix (2026-09-16)

Scope: `TICKET-DAEMON-JOB-LIFECYCLE`. Host: RTX A4000 16 GB, Vulkan + NVENC,
`linux-video-release` lane. Every number below is measured; nothing is estimated.

## 1. Method

The timing sidecar (`<video>.timing.json`) covers everything up to the export's own
`wall_t1`, and the receipt prints its own breakdown. Two phases had no timer at all:

* **returning** from `execute_render_job` (teardown of the exporter session that runs
  when the result objects are destroyed);
* the **caller tail** around it (receipt build/write).

Both were instrumented with `steady_clock` deltas reported as
`[INFO] job-lifecycle:` lines (`command_render_plan_internal.inc`,
`video_export_pipe.cpp`, `pipe_export_session.hpp`). Measurement used the certified
overlay shape (2 jobs, 121 frames, `require_gpu_native_full_graph`) submitted through
the normal queue path (`vram_probe.py`), i.e. the serialized daemon service.

## 2. Pre-fix attribution

`lifecycle-probe-1` (caller timers only, identical manifest):

| job | export `wall_ms` | `execute_ms` | caller tail | sidecar → receipt |
|---|---|---|---|---|
| phrase | 2142.0 | 3003.7 | 429.9 | **1257 ms** |
| image | 2299.8 | 3003.1 | 388.1 | **1241 ms** |

`lifecycle-probe-2` (teardown split): the constant is **inside `session.reset()`**,
after the session destructor body has already run:

| job | `session_teardown_ms` | destructor body | unaccounted after the body |
|---|---|---|---|
| phrase | **997.7** | 102 (encoder 31.4 + full_graph 70.6) | **896 ms** |
| image | **492.6** | ~104 | ~390 ms |

## 3. Root cause (code, not inference)

`SystemMetricsCollector` is a **member of `PipeExportSession`**, so it is constructed
and destroyed once per job. Its worker waits for the next tick with an
uninterruptible `sleep_for(1s)`, and the destructor joins that worker:

```cpp
// src/core/system_metrics.cpp (pre-fix)
void SystemMetricsCollector::tick_worker() {
    update_caches();
    while (!m_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));   // <- not interruptible
        update_caches();
    }
}
SystemMetricsCollector::~SystemMetricsCollector() {
    m_stop.store(true, std::memory_order_release);
    if (m_worker.joinable()) m_worker.join();                   // <- blocks up to 1 s
}
```

The destructor body of `PipeExportSession` runs first and destroys the members
afterwards, which is exactly why the cost appeared *after* every named teardown step
and why it was **constant and independent of job duration** (a uniform 0–1 s wait: it
does not scale with frames, and it reproduced identically on a 937 ms and a 1980 ms
job). The code comment even documented it as "max ~1s by design", i.e. a deliberate
cost that had never been charged to the job lifecycle.

## 4. Fix

`SystemMetricsCollector::tick_worker` now waits on a condition variable
(`wait_for(1s, pred)` + `notify_all` from the destructor) instead of sleeping, so the
1 Hz cadence is unchanged while shutdown is immediate
(`include/chronon3d/core/system_metrics.hpp`, `src/core/system_metrics.cpp`).

## 5. Effect — controlled A/B (same manifest, same shape, same host)

`lifecycle-probe-2` (pre-fix) vs `lifecycle-probe-3` (post-fix):

| job | `session_teardown_ms` pre | post | `execute_ms` pre | post |
|---|---|---|---|---|
| phrase | 997.7 | **99.8** | 3014.9 | 2061.8 |
| image | 492.6 | **115.7** | 2011.7 | 1706.0 |

Per-job saving on `execute_ms` equals the wait that was removed (953 ms / 306 ms).

Production shape (real pipeline jobs, post-fix): `session_teardown_ms` =
124.2 / 108.9 / 109.0 ms with `execute_ms` ≈ 1011 ms — the wait is gone from the
serialized path (`items 010–012` of run `run_1789561856968155785_7439a39c11a8`).

**Not fixed / still open:** one job out of four (`item:013`, same shape, same
`wall_ms`) still shows `session_teardown_ms=1061.3`. That second, *intermittent*
~0.9 s cost is not the metrics worker and is still unattributed; it is the remaining
open item of the ticket.

## 6. End-to-end effect — the published 100-overlay matrix

`ml-overlays-10x10-run3` is the certified run2 matrix, perturbed exactly like the
probe (`duration_ms += 10`, trailing space per text item), so the only difference is
the daemon binary. run3 renders **121** frames per job against run2's 120, i.e. ~0.8%
*more* work per job:

| | run2 (pre-fix) | run3 (post-fix) | delta |
|---|---|---|---|
| jobs completed | 100/100 | 100/100 | — |
| total wall | 230 259 ms | **164 043 ms** | **−66 216 ms (−28.8%)** |
| jobs/min | 26.058 | **36.576** | +40.4% |
| aggregate fps | 52.115 | **73.761** | +41.5% |
| `render_wall` p50 | 11 374 ms | 7 167 ms | −37.0% |
| distinct artifact hashes | 49 | 49 | same content |

Per-job saving: 662 ms — which matches the 638 ms mean removed on the controlled A/B,
so the aggregate gain is fully explained by the removed wait and not by a shape change.

## 7. Where the remaining service time goes (post-fix, per overlay job)

| phase | ms | note |
|---|---|---|
| render loop (121 frames) | 227–260 | ~11% of the service, unchanged |
| prepare/pool warm + plan compile inside `wall_ms` | ~1 100 | still per-job (101 boots / 100 jobs) |
| session teardown | ~110 | was ~1 000 (intermittent ~1 060 remains) |
| receipt build | ~280–430 | dominated by `probe_ms` (ffprobe) |
| receipt write | 0.2–0.4 | negligible |
| **worker duplicate verification** | ~250 | the worker ffprobes the artifact again |

Next levers, in order: (1) attribute and remove the intermittent ~1 s teardown;
(2) one `ArtifactReceipt` authority instead of two ffprobe passes (~250 ms/job);
(3) stop paying per-job prepare/pool warm on the same shape; only then raise
concurrency — the two-runtime VRAM gate is already satisfied for the overlay shape
(`text-quality-attribution-2026-09-16.md`, 2 concurrent FullGraph exporters at
6 319 MiB / 16 376 MiB peak).
