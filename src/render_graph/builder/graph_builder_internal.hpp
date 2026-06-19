#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <vector>
#include <memory_resource>

namespace chronon3d {
    class Scene;
    struct Layer;
}

namespace chronon3d::graph {
    struct RenderGraphContext;
    struct LayerGraphItem;
}

// LayerResolutionResult and resolve_layers() are now in
// <chronon3d/render_graph/layer/layer_resolver.hpp>.
