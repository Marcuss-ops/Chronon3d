# Phase 4 — concurrent determinism evidence

Status: **PASS** for the Vulkan render contract exercised here.

The same `DarkGridBackground` frame (`frame=0`) was rendered twice at each
of 1, 4, and 8 scheduler workers using
`CHRONON3D_SCHEDULER_WORKERS`. Every render exited `0` and produced the same
PNG SHA-256:

```text
c3bccaa672800bb37f62796fe37a827dac1ede176ab89b6fd0026d4854eb497a
```

That hash was observed for all six outputs (`w1-r1`, `w1-r2`, `w4-r1`,
`w4-r2`, `w8-r1`, `w8-r2`). The run directory was
`/tmp/chronon-phase4-determinism-workers.jvocOr`.

This is a byte-identical output claim for the Vulkan corpus and worker
settings recorded above; broader BitExact certification still belongs to the
CHRONON_PLAN Stage 4 corpus.
