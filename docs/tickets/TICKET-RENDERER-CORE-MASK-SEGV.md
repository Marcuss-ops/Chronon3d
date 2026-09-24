# Renderer core tests: intermittent mask-render SIGSEGV

## Reproduction and scope

On 2026-09-24, repeated full runs of
`build/chronon/linux-fast-dev/tests/chronon3d_renderer_core_tests` intermittently
terminated with SIGSEGV in `tests/scene/shapes/mask_tests.cpp`. The reported
case varied between the rounded-rectangle mask, inverted circle mask, and path
mask render integration. Each reported test passes when selected alone. One
full run also passed under GDB, so no useful stack trace has been captured yet.

## Regression check

The working changes for Echo and Keying were saved, fully reverted, and the
renderer-core target was rebuilt from the repository baseline. The full suite
still crashed during a repeated run, this time in the rounded-rectangle mask
case. The changes were then restored. This failure predates the Echo/Keying
work and is not attributed to those effects.

## Status and next steps

OPEN — existing intermittent failure; root cause unknown. Capture a usable
backtrace or run under ASan/TSan, then inspect software-renderer/task-scheduler
teardown and shared render-session state used by the mask tests. Exit only when
the full target passes repeated runs and the mask cases remain green in-suite.
