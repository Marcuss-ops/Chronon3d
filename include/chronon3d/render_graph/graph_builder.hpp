#pragma once

#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/basic_nodes.hpp>
#include <chronon3d/scene/scene.hpp>
#include <chronon3d/scene/layer.hpp>

namespace chronon3d::graph {

class GraphBuilder {
public:
    static RenderGraph build(const Scene& scene, const RenderGraphContext& ctx) {
        RenderGraph graph;
        
        // Start with a clear canvas
        GraphNodeId current = graph.add_node(std::make_unique<ClearNode>());
        
        // Add root nodes (direct RenderNodes in Scene)
        for (const auto& node : scene.nodes()) {
            cache::NodeCacheKey source_key{
                .scope = "root.source",
                .frame = ctx.frame,
                .width = ctx.width,
                .height = ctx.height,
                .params_hash = 0, // Placeholder
                .source_hash = 0 // Placeholder
            };
            
            auto source = graph.add_node(std::make_unique<SourceNode>(
                std::string(node.name), node, source_key
            ));
            
            current = graph.add_node(std::make_unique<CompositeNode>(BlendMode::Normal));
            graph.connect(current - 2, current); // Old current (canvas)
            graph.connect(source, current);      // New source
        }
        
        // Add layers
        for (const auto& layer : scene.layers()) {
            if (!layer.active_at(ctx.frame)) continue;
            
            if (layer.kind == LayerKind::Normal) {
                // Simplified Normal Layer: Source -> Effects -> Composite
                // 1. Source
                cache::NodeCacheKey source_key{
                    .scope = "layer.source",
                    .frame = ctx.frame,
                    .width = ctx.width,
                    .height = ctx.height,
                    .params_hash = 0,
                    .source_hash = 0 
                };
                
                // For simplicity in this MVP, we treat layer nodes as a single SourceNode-like operation
                // In reality, we'd iterate over layer.nodes.
                auto source = graph.add_node(std::make_unique<ClearNode>()); // Placeholder for complex layer render
                
                // 2. Mask
                if (layer.mask.enabled()) {
                    auto masked = graph.add_node(std::make_unique<MaskNode>(layer.mask));
                    graph.connect(source, masked);
                    source = masked;
                }
                
                // 3. Effects
                if (!layer.effects.empty()) {
                    auto effects = graph.add_node(std::make_unique<EffectStackNode>(layer.effects));
                    graph.connect(source, effects);
                    source = effects;
                }
                
                // 4. Composite
                auto composite = graph.add_node(std::make_unique<CompositeNode>(layer.blend_mode));
                graph.connect(current, composite); // Bottom (canvas)
                graph.connect(source, composite);  // Top (layer)
                current = composite;
                
            } else if (layer.kind == LayerKind::Adjustment) {
                // Adjustment Layer: EffectStack applied to current canvas
                auto adj = graph.add_node(std::make_unique<AdjustmentNode>(layer.effects));
                graph.connect(current, adj);
                current = adj;
            }
        }
        
        return graph;
    }
};

} // namespace chronon3d::graph
