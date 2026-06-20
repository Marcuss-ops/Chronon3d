#pragma once

#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/runtime/execution_plan_cache.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>

#include <memory>

namespace chronon3d {

/**
 * RendererRuntimeResources — executor, registries, and stateless caches
 * that are created once and remain alive for the lifetime of the renderer.
 *
 * TICKET-009 — `plan_cache` is split off from `GraphExecutor` so the
 * executor itself is stateless; the cache lives with the renderer so it
 * can outlive transient executor invocations.  Will move into the future
 * `RenderRuntime` / `RenderEngine::Impl` (TICKET-011) where lifetime
 * ownership becomes explicit.
 */
struct RendererRuntimeResources {
    std::unique_ptr<renderer::SoftwareRegistry>  software_registry;
    std::unique_ptr<graph::GraphExecutor>         executor;
    /// TICKET-009 — topological-plan cache.  Lifetime matches the renderer;
    /// shared between this renderer and any spawned takers.
    std::shared_ptr<runtime::ExecutionPlanCache> plan_cache;
    std::unique_ptr<graph::GraphNodeCatalog>      graph_node_registry;
    std::unique_ptr<effects::EffectCatalog>       effect_catalog;
};

} // namespace chronon3d
