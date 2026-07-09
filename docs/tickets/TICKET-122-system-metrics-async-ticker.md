# TICKET-122 — Async 1Hz /proc Reads Ticker (perf: hot-path decoupling)

| Field      | Value                                                    |
|------------|----------------------------------------------------------|
| Status     | OPEN                                                      |
| Date       | 2026-07-09                                                |
| Deciders   | Agent3 (Buffy), awaiting code-review sign-off             |
| Tags       | performance, telemetry, /proc, hot-path, atomic           |
| Related    | AGENTS.md §Cat-2 freeze (no new public API), AGENTS.md ADR-freeze (no new singleton/registry/resolver without ADR), `src/core/system_metrics.{cpp,hpp}`, `src/runtime/telemetry/telemetry_manager.cpp` |

## Context

The RenderEngine hot-path subscription `RenderEngine::fill_system_counters`
called the following per-snapshot synchronous /proc reads from
`src/core/system_metrics.cpp`:

- `/proc/self/status` (Voluntary_ctxt_switches + NonVoluntary_ctxt_switches) — line 50
- `/proc/self/stat`  (page faults)                                       — line 177
- `/proc/self/stat`  (utime/stime CPU jiffies)                         — line 216
- `/proc/self/statm` (resident pages → RSS)                             — line 229
- `/proc/meminfo`    (MemTotal + MemAvailable)                         — line 262

Plus a separate synchronous `/proc/self/status` (VmHWM) read on every
`TelemetryManager::get_peak_memory_usage()` call from
`src/runtime/telemetry/telemetry_manager.cpp:283`.

Worst case: up to six synchronous file opens + line-scans on every
telemetry snap, blunting the RenderEngine hot-path latency budget.

## Goal (single commit on main)

A single ~1Hz background `std::thread` worker per collector owns all
/proc reads and writes atomics; public methods serve the cached atomics
(up to ~1s stale, never blocking on /proc).  This blunts the latency on
the RenderEngine hot-path subscriber `fill_system_counters`.

## Decision

### Spec

- **No new public API symbols** (AGENTS.md §Cat-2 OK — only private
  members added; public method signatures preserved verbatim).
- **No new singleton/registry/resolver** (AGENTS.md §ADR-freeze OK — the
  worker thread is owned by the collector instance, not a global).
- **Background cadence**: 1 Hz.  Implementation: simple
  `std::this_thread::sleep_for(std::chrono::seconds(1))` between
  read sweeps (cv-based shutdown would be over-engineered at 1 Hz).
- **Atomic cache fields** (all `std::memory_order_relaxed`):
  - 4 × `std::atomic<uint64_t>` for ProcessMetrics
  - 2 × `std::atomic<uint64_t>` for ProcessCpuTime
  - 1 × `std::atomic<uint64_t>` for peak RSS (monotonic via CAS retry)
  - 2 × `std::atomic<uint64_t>` for SystemMemory
  - 2 × `std::atomic<double>`   for ffmpeg CpuSplit
- **Worker lifecycle**:
  - ctor: `m_worker = std::thread(&SystemMetricsCollector::tick_worker, this);`
  - dtor: `m_stop.store(true, ...); m_worker.join(); close_llc_counters();`
- **Public method bodies** (signature preserved):
  - `process_metrics()`, `process_cpu_time()`, `process_rss_bytes()`,
    `system_memory()`, `ffmpeg_cpu_split()` now read atomic cache
- **TelemetryManager::get_peak_memory_usage()**: inlined a TU-private
  `PeakMemoryCache` static struct in `telemetry_manager.cpp` with its
  own worker, avoiding any new method on `SystemMetricsCollector`.

### Anti-non-goals

- No new public API symbol (Cat-2).
- No relocations of `SystemMetricsCollector`'s cache to a global singleton
  (would conflict with the multi-instance pattern when tests want isolated
  collectors).
- No design changes to the perf-event LLC counters (those use direct
  `::read` syscalls and don't need ticker-style caching).
- No 100% freshness guarantee — by design, ~1s staleness is acceptable.

## Risk

- Worker shutdown takes up to ~1s in the worst case (worker mid-sleep).
  Acceptable for collector destructor / program-exit paths.
- `std::atomic<double>` is lock-free on x86-64 (verified).  If targeting
  exotic platforms without lock-free `atomic<double>`, the cache falls
  back to `std::atomic<uint64_t>` bit-casts; not needed here.
- First-tick populate happens before the sleep loop, so the first
  call into `process_metrics()` etc. observes non-zero values (modulo
  kernel reporting).
- `ctx.frame_input.has_camera_2_5d`-style race semantics: the cache
  is monotonic for `m_cached_rss_bytes`; consumers see the most recent
  successful read or zero on the first tick before populate completes.

## Closure lineage

- Phase 1 closes today once all 6 reads are tickerised (5 in
  `SystemMetricsCollector` + 1 in `TelemetryManager`).
- Phase 2 (follow-up, gated on this PR): add a unit test that drives a
  `SystemMetricsCollector` through deterministic time and asserts the
  cache updates exactly once per tick.
- Phase 3 (future): consider cv-based shutdown for sub-second destructor
  if profiling shows it matters on CI.
