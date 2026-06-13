#pragma once

#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>

#include <memory>

namespace chronon3d {

/**
 * RendererRuntimeResources — executor and registry that are created once
 * and remain alive for the lifetime of the renderer.
 */
struct RendererRuntimeResources {
    std::unique_ptr<renderer::SoftwareRegistry> software_registry;
    std::unique_ptr<graph::GraphExecutor>       executor;
};

} // namespace chronon3d
