# Chronon runtime ownership matrix

This is the Wave 9 gate. Every mutable runtime field has exactly one lifetime
owner. Other lifetimes may borrow it, but may not own or mutate it.

| Field / responsibility | Process | Job | Frame |
|---|:---:|:---:|:---:|
| Vulkan instance and device | ✅ | | |
| CUDA context and stream registry | ✅ | | |
| pipeline cache | ✅ | | |
| GPU asset cache | ✅ | | |
| glyph atlas | ✅ | | |
| device scheduler | ✅ | | |
| graph and kernel registries | ✅ | | |
| video compositor registry | ✅ | | |
| telemetry sink | ✅ | | |
| immutable CompiledProgram | | ✅ | |
| input assets and output configuration | | ✅ | |
| decoder and encoder sessions | | ✅ | |
| frame range and receipt policy | | ✅ | |
| cancellation and job counters | | ✅ | |
| resource bindings | | ✅ | |
| frame index and presentation time | | | ✅ |
| FrameSlotLease | | | ✅ |
| scratch arena | | | ✅ |
| ExecutionState | | | ✅ |
| temporary bindings and output surface | | | ✅ |
| frame counters | | | ✅ |

`RuntimeContext`, `JobSession`, and `FrameExecution` are the typed seams for
these three columns. The old runtime facade remains a compatibility bridge;
it is not a second owner. A future migration must move a field to one column
before deleting that bridge.
