#pragma once

#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>

#include <memory>

namespace chronon3d {

/**
 * RendererRuntimeResources — executor and registries that are created once
 * and remain alive for the lifetime of the renderer.
 */
struct RendererRuntimeResources {
    std::unique_ptr<renderer::SoftwareRegistry> software_registry;
    std::unique_ptr<graph::GraphExecutor>       executor;
    std::unique_ptr<graph::GraphNodeCatalog>    graph_node_registry;
    std::unique_ptr<effects::EffectCatalog>     effect_catalog;
};

} // namespace chronon3d
