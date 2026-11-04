# P0-12 CPU/GPU lifecycle and sanitizer coverage — 2026-11-04

## CPU sanitizer check

Compiled a focused `FrameArena` exercise with Clang 21 and
`-fsanitize=address,undefined -fno-omit-frame-pointer`; no sanitizer findings.
It performed 10,000 allocate/reset cycles with a strict arena, exercised
pre-use reserve, and verified that strict-capacity overflow throws
`ArenaOverflow` instead of allocating past the bound. The harness was written
under `/tmp` and did not alter production source.

## GPU job lifecycle evidence

The A4000 Vulkan CLI lifecycle checks are recorded in
[`p009-vulkan-job-lifecycle-20261104.md`](p009-vulkan-job-lifecycle-20261104.md):
100 validated sequential jobs, a concurrent pair, and cancellation/teardown
completed without Vulkan validation errors or leaked partial raw output.

## Remaining acceptance work

This does not run the renderer/runtime under ASan/UBSan, does not measure peak
or retained CPU/GPU memory across full jobs, and does not prove fail-closed
admission under VRAM pressure. P0-12 therefore remains PARTIAL.
