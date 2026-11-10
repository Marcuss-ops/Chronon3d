# Typed RenderPlan particle parity — 2026-11-09

## Software pixel equivalence

Plan: [`c2-1000.plan.json`](../lights-shadows/T05-native-vulkan-20261029/c2-1000.plan.json), SHA-256
`f8bcb7262960d7ef1b69a843ea53d549741d8cdd23ccdb8a9b6d813840854a20`.
Both RGBA raw files are 1920×1080×4 bytes. The first was captured through the
legacy RenderPlan particle route before its `CompiledComposition` fallback was
removed; the second was rendered by the typed `CompiledProgram` route.

`cmp` reports byte identity; both SHA-256 values are
`87e9ac05c05a3152c0634f810e51cee5f8822d4b8c724c680fc99dedfcf01607`.

## Strict-native Vulkan execution

The typed route completed the same 1-frame plan with
`--backend vulkan --gpu-hot-path-mode require_gpu_native --video-sink raw`.
The output is retained as [`typed-vulkan.raw`](typed-vulkan.raw), SHA-256
`ab409c9ecef064c1e2fc1377c9f030ef40f4ac9dc0b59674496a1b7ddcb7ed93`; the
per-frame telemetry is [`typed-vulkan.timing.json`](typed-vulkan.timing.json).
The verified binary SHA-256 was
`92247be1bfe18ccc50a5021e48641cedf9757cc281368d7c20e584acb58b88f4`.

This proves strict-native route execution, not CPU/Vulkan particle pixel
parity. Multi-frame animation, sprite particles, mixed alpha, bokeh and
reverse-order rendering remain open equivalence cases.
