#pragma once

#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>

#include <memory>

namespace chronon3d {

/**
 * RendererRuntimeResources — executor and registries that are created once
 * and remain alive for the lifetime of the renderer.
 *
 * PR-B: `scheduler` (unique_ptr<ExecutionScheduler>) lives here, next to
 * `executor`.  SoftwareRenderer constructs the scheduler from the per-instance
 * `Config::SchedulerConfig` and exposes it through a get-only accessor; the
 * scheduler is then propagated to every executor call site either
 * explicitly (preferred for clarity) or via `ctx.resources.scheduler`
 * (used by PrecompNode, which has no direct access to the renderer).
 */
struct RendererRuntimeResources {
    // ── Original field order is LOAD-BEARING for ABI stability of
    // downstream offsets in SoftwareRenderer (cf. comment in
    // software_renderer.hpp near m_backend).  PR-B appends the scheduler
    // field AT THE END to keep the existing slot offsets unchanged.
    std::unique_ptr<renderer::SoftwareRegistry> software_registry;
    std::unique_ptr<graph::GraphExecutor>       executor;
    std::unique_ptr<graph::GraphNodeCatalog>    graph_node_registry;
    std::unique_ptr<effects::EffectCatalog>     effect_catalog;

    std::unique_ptr<ExecutionScheduler>         scheduler;        // PR-B (appended for ABI)
};

} // namespace chronon3d
