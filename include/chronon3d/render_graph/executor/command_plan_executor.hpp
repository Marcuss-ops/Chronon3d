#pragma once

// ---------------------------------------------------------------------------
// render_graph/executor/command_plan_executor.hpp
//
// Backend-neutral executor for the runtime GPU CommandPlan.  It plays the
// frame orchestrator role for a compiled plan:
//
//   bind_plan_slots(plan.resources, registry)   // alias physical slots
//   backend.begin_plan_batch(plan.barriers)     // open the frame batch
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
///
/// Steps, in order:
///   1. bind_plan_slots(plan.resources, registry) — propagates the physical
///      slot aliasing onto the surface registry's identity records;
///   2. backend.begin_plan_batch(plan.barriers) — opens the frame batch with
///      plan-driven barrier synchronization (no-op on non-batching backends);
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
