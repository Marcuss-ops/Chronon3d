#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/math/transform.hpp>
#include <vector>

namespace chronon3d::graph {

// Intermediate representation used during 2.5D graph construction.
struct LayerGraphItem {
    const Layer* layer{nullptr};
    Transform    transform{};      // projected transform (or raw transform for 2D layers)
    Mat4         projection_matrix{1.0f};
    f32          depth{0.0f};      // world depth (layer.z - camera.z); 0 for 2D layers
    bool         projected{false}; // true if transform was computed by project_layer_2_5d
    usize        insertion_index{0};
};

class GraphBuilder {
public:
    static RenderGraph build(const Scene& scene, const RenderGraphContext& ctx);
};

} // namespace chronon3d::graph
