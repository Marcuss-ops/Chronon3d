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

## Per-frame parameter ownership (B.1)

Three containers previously shared the `FrameParameterBlock` name.  They are
three different axes, not three implementations of one cache:

| Axis | Sole owner | Consumers |
|---|---|---|
| Whole-frame sampled layer payload | `compiler::FrameParameterBlock` (`compiler/frame_parameters.hpp`) | `TypedProgramSampler`, `RenderPipeline`, `FrameSlotPayload`, `RenderGraphContext::frame_parameters` |
| Per-binding transform/effect slots for one nested Precomp | `graph::PerBindingTransformBlock` (`render_graph/pipeline/per_binding_transform_block.hpp`) | `PrecompNode::m_transform_block` only |
| Per-frame-time slice table | `graph::FrameParameterTable` (`render_graph/pipeline/frame_parameter_table.hpp`) | `CompiledFrameGraph` / `GPUCommandPlan` / `CompiledComposition` |

The `compiler::FrameParameterBlock` remains the canonical authority for
sampled layer values.  `PerBindingTransformBlock` is deliberately named for
its narrower ownership and must not grow sampled-layer payloads.  A new
per-frame field belongs to exactly one of these rows; it must not be copied
into a second container.

## Camera surface authority

Camera intent is authored in ChrononMotion3D (`CameraRig`); C3D only consumes
resolved state. Inside C3D the camera surface has one owner per fact:

| Fact | Sole owner | Notes |
|---|---|---|
| Admission ranges (FOV, focal, sensor, f-stop, DoF) | ✅ `render_plan/camera_contract.hpp` + `math/camera_projection_contract.hpp` | no other site restates the ranges |
| Projection implementation | ✅ `math/camera_projection_resolver.hpp` | matrix/clip/frustum live in `math/detail/`, not SDK API |
| 2.5D projection | ✅ `math/camera_2_5d_projection.hpp` + `scene/model/camera/camera_2_5d.hpp` | runtime paths and tests are the consumers |
| DoF/shutter implementation | ✅ `render_graph/nodes/per_pixel_dof_node.*` + `scene/model/camera/dof.hpp` | no editorial recipes in C3D |
| Diagnostics | ✅ `scene/camera/*`, `src/core/camera_2_5d_log.cpp` | consumers only; no second camera state |

A new C3D camera file or field must change pixels, have a real consumer, and
carry exactly one authored unit across the boundary. Editorial moves belong to
CM3D presets/packs. See `docs/evidence/camera-surface-consolidation/` and the
`S0` guardrail in `ChrononMotion3D/docs/BOUNDARY_C3D.md`. <!-- drift-class: third-party — sibling-repo citation -->
