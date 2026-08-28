#pragma once

// ---------------------------------------------------------------------------
// tests/helpers/command_plan_executor.hpp
//
// TEST-ONLY declaration for the backend adapter for the runtime GPU
// CommandPlan. It is not part
// of the production GraphExecutor call graph; see docs/EXECUTOR_OWNERSHIP.md.
// Backend-neutral executor for the runtime GPU CommandPlan.  It plays the
// frame orchestrator role for a compiled plan:
//
//   bind_plan_slots(plan.resources, registry)   // alias physical slots (identity)
//   backend.begin_plan_batch(plan)              // open the batch + bind backing
//   for each pass: one canonical RenderBackend surface operation
//   backend.end_frame_batch()                   // single frame submission
//
// Every pass dispatches through the CANONICAL graph::RenderBackend surface
// API (composite_surfaces, transform_surface, blur_surface, ...); no Vulkan
// type leaks in and no second backend path is introduced.
// ---------------------------------------------------------------------------

#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/render_surface.hpp>

namespace chronon3d::runtime {

/// Execute a backend-neutral CommandPlan on a batching-capable backend.
/// Test/readiness harness API; production rendering uses GraphExecutor.
///
/// Steps, in order:
///   1. bind_plan_slots(plan.resources, registry) — propagates the physical
///      slot aliasing onto the surface registry's identity records;
///   2. backend.begin_plan_batch(plan) — opens the frame batch with
///      plan-driven barrier synchronization AND binds the backend's physical
///      slots (one backing image per slot) from plan.resources (no-op on
///      non-batching backends);
///   3. one canonical RenderBackend surface operation per GpuPass, in plan
///      order;
///   4. backend.end_frame_batch() — the single submission for the frame.
///
/// The batch is always closed, even when a pass fails, so the backend is not
/// left in an active-batch state.  Returns false when any pass reported a
/// backend error.
[[nodiscard]] bool execute_command_plan(graph::RenderBackend& backend,
                                        RenderSurfaceRegistry& registry,
                                        const CommandPlan& plan);

} // namespace chronon3d::runtime
