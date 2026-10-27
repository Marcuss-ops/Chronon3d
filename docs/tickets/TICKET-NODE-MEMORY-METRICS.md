# TICKET-NODE-MEMORY-METRICS — per-node memory accounting

**Status: CLOSED (2026-10-27).** The runtime counters, node telemetry, CLI
report, local validation gate, and focused CPU verification are implemented.
The missing B00–B11 content pack is not needed to prove the instrumentation
works, so corpus certification is no longer a release blocker.

## Delivered

- Per-node counters cover pixel/byte reads and writes, framebuffer copies and
  clears, observed backing-store allocations, and temporary buffers.
- `--node-memory-json` writes the post-warmup per-node report.
- `tools/check_node_memory_metrics.sh` validates reports and fails on measured
  hot-node allocations. The local self-test covers clean, allocating, and
  missing-report cases.
- Local evidence: `chronon3d_memory_tests` passed 12/12 cases (111 assertions);
  detailed telemetry integration passed 1/1 case (11 assertions). CPU smoke
  reports for `DarkGridBackground` and `CameraImageClip` are recorded in the
  original ticket history and remain smoke evidence only.

## Measurement limits

The allocation counters report instrumented framebuffer events and explicit
`record_allocation()` calls; they are not a global C++ heap profiler. The
external B00–B11 pack and old `examples/bench_corpus` runner are not present in
this checkout. A corpus baseline can be proposed separately if that pack is
restored; it does not block shipping the implemented telemetry.

Implementation decision and schema: [ADR-026](../adr/ADR-026-node-memory-metrics.md).
Primary implementation: `include/chronon3d/runtime/telemetry/render_telemetry_record.hpp`,
`include/chronon3d/render_graph/executor/node_memory_metrics.hpp`, and
`tools/check_node_memory_metrics.sh`.
