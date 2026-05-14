#include <chronon3d/renderer/software/render_graph.hpp>
#include <chronon3d/renderer/software/software_renderer.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <chronon3d/core/profiling.hpp>
#include "../primitive_renderer.hpp"

#include <fmt/format.h>
#include <bit>
#include <iomanip>
#include <sstream>
#include <utility>
#include <type_traits>
#include <variant>

namespace chronon3d::rendergraph {

namespace {

struct GraphNode final : RenderNode {
    GraphNode(render_graph::NodeKind kind,
              std::string label,
              render_graph::RenderCacheKey key,
              std::vector<NodeId> inputs)
        : m_kind(kind)
        , m_label(std::move(label))
        , m_key(std::move(key))
        , m_inputs(std::move(inputs)) {}

    [[nodiscard]] render_graph::NodeKind kind() const override { return m_kind; }
    [[nodiscard]] std::string_view label() const override { return m_label; }
    [[nodiscard]] const render_graph::RenderCacheKey& cache_key() const override { return m_key; }
    [[nodiscard]] std::span<const NodeId> inputs() const override { return m_inputs; }

    render_graph::NodeKind m_kind;
    std::string m_label;
    render_graph::RenderCacheKey m_key;
    std::vector<NodeId> m_inputs;
};

struct GraphPass final : RenderPass {
    GraphPass(render_graph::RenderPassKind kind,
              std::string label,
              render_graph::RenderCacheKey key,
              bool cacheable,
              RenderGraph::PassExecutor executor)
        : m_kind(kind)
        , m_label(std::move(label))
        , m_key(std::move(key))
        , m_cacheable(cacheable)
        , m_executor(std::move(executor)) {}

    [[nodiscard]] render_graph::RenderPassKind kind() const override { return m_kind; }
    [[nodiscard]] std::string_view label() const override { return m_label; }
    [[nodiscard]] const render_graph::RenderCacheKey& cache_key() const override { return m_key; }
    [[nodiscard]] bool cacheable() const override { return m_cacheable; }
    [[nodiscard]] RenderPassResult execute(::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                                           RenderGraphExecutionState& state,
                                           NodeId self) const override {
        return m_executor(ctx, state, self);
    }

    render_graph::RenderPassKind m_kind;
    std::string m_label;
    render_graph::RenderCacheKey m_key;
    bool m_cacheable{false};
    RenderGraph::PassExecutor m_executor;
};

} // namespace

NodeId RenderGraph::add_entry(std::unique_ptr<RenderNode> node, std::unique_ptr<RenderPass> pass) {
    const NodeId id = m_passes.size();
    m_passes.push_back({std::move(node), std::move(pass)});
    return id;
}

NodeId RenderGraph::add_output(std::string label, RenderCacheKey key, RenderState state) {
    auto node = std::make_unique<GraphNode>(RenderNodeKind::Output, std::move(label), std::move(key), std::vector<NodeId>{});
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Output,
        std::string(node->label()),
        node->cache_key(),
        true,
        [state](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                RenderGraphExecutionState&, NodeId) {
            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            return RenderPassResult{.framebuffer = std::move(fb), .state = state, .has_state = true};
        });
    return add_entry(std::move(node), std::move(pass));
}

NodeId RenderGraph::add_transform(std::string label,
                                  RenderCacheKey key,
                                  NodeId input,
                                  Transform transform) {
    auto node = std::make_unique<GraphNode>(RenderNodeKind::Transform, std::move(label), std::move(key), std::vector<NodeId>{input});
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Transform,
        std::string(node->label()),
        node->cache_key(),
        false,
        [transform, input](::chronon3d::rendergraph::RenderGraphExecutionContext&,
                           RenderGraphExecutionState& state, NodeId) {
            RenderPassResult result;
            RenderState base_state{Mat4(1.0f), 1.0f};
            if (auto it = state.results.find(input); it != state.results.end() && it->second.has_state) {
                base_state = it->second.state;
                result.framebuffer = it->second.framebuffer;
            }
            
            result.state = combine(base_state, transform);
            result.has_state = true;
            return result;
        });
    return add_entry(std::move(node), std::move(pass));
}

NodeId RenderGraph::add_source(std::string label,
                               RenderCacheKey key,
                               NodeId input,
                               const ::chronon3d::RenderNode& node) {
    auto inputs = std::vector<NodeId>{input};
    auto graph_node = std::make_unique<GraphNode>(RenderNodeKind::Source, std::move(label), std::move(key), inputs);
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Source,
        std::string(graph_node->label()),
        graph_node->cache_key(),
        true,
        [node, input](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                      RenderGraphExecutionState& state, NodeId) {
            RenderState current{Mat4(1.0f), 1.0f};
            if (auto it = state.results.find(input); it != state.results.end() && it->second.has_state) {
                current = it->second.state;
            }

            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            ctx.renderer.draw_node(*fb, node, current, ctx.camera, ctx.width, ctx.height);

            RenderPassResult result;
            result.framebuffer = std::move(fb);
            result.state = current;
            result.has_state = true;
            return result;
        });
    return add_entry(std::move(graph_node), std::move(pass));
}

NodeId RenderGraph::add_layer_source(std::string label,
                                     RenderCacheKey key,
                                     NodeId input,
                                     const Layer& layer) {
    auto inputs = std::vector<NodeId>{input};
    auto graph_node = std::make_unique<GraphNode>(RenderNodeKind::Source, std::move(label), std::move(key), inputs);
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Source,
        std::string(graph_node->label()),
        graph_node->cache_key(),
        true,
        [layer, input](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                       RenderGraphExecutionState& state, NodeId) {
            RenderState current{Mat4(1.0f), 1.0f};
            if (auto it = state.results.find(input); it != state.results.end() && it->second.has_state) {
                current = it->second.state;
            }

            if (layer.mask.enabled()) {
                current.mask = &layer.mask;
                current.layer_inv_matrix = glm::inverse(current.matrix);
            }

            auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
            fb->clear(Color::transparent());
            ctx.renderer.render_layer_nodes(*fb, layer, current, ctx.camera, ctx.width, ctx.height);

            RenderPassResult result;
            result.framebuffer = std::move(fb);
            result.state = current;
            result.has_state = true;
            return result;
        });
    return add_entry(std::move(graph_node), std::move(pass));
}

NodeId RenderGraph::add_effect(std::string label,
                               RenderCacheKey key,
                               NodeId input,
                               const Layer& layer) {
    auto graph_node = std::make_unique<GraphNode>(RenderNodeKind::Effect, std::move(label), std::move(key), std::vector<NodeId>{input});
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Effect,
        std::string(graph_node->label()),
        graph_node->cache_key(),
        true,
        [layer, input](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                       RenderGraphExecutionState& state, NodeId) {
            RenderPassResult result;
            if (auto it = state.results.find(input); it != state.results.end()) {
                result = it->second;
            }

            if (!result.framebuffer) {
                result.framebuffer = std::make_shared<Framebuffer>(ctx.width, ctx.height);
                result.framebuffer->clear(Color::transparent());
            } else {
                result.framebuffer = std::make_shared<Framebuffer>(*result.framebuffer);
            }

            if (!layer.effects.empty()) {
                ctx.renderer.apply_effect_stack(*result.framebuffer, layer.effects);
            }

            return result;
        });
    return add_entry(std::move(graph_node), std::move(pass));
}

NodeId RenderGraph::add_glass(std::string label,
                               RenderCacheKey key,
                               NodeId input,
                               const Layer& layer) {
    auto graph_node = std::make_unique<GraphNode>(RenderNodeKind::Effect, std::move(label), std::move(key), std::vector<NodeId>{input});
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Effect,
        std::string(graph_node->label()),
        graph_node->cache_key(),
        true,
        [layer, input](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                       RenderGraphExecutionState& state, NodeId) {
            RenderPassResult result;
            if (auto it = state.results.find(input); it != state.results.end()) {
                result = it->second;
            }

            if (!result.framebuffer) {
                result.framebuffer = std::make_shared<Framebuffer>(ctx.width, ctx.height);
                result.framebuffer->clear(Color::transparent());
            }

            // 1. Create a blurred version of the background
            auto blurred = std::make_shared<Framebuffer>(*result.framebuffer);
            f32 blur_radius = 15.0f;
            for (const auto& inst : layer.effects) {
                if (std::holds_alternative<BlurParams>(inst.params)) {
                    blur_radius = std::get<BlurParams>(inst.params).radius;
                }
            }
            ctx.renderer.apply_blur(*blurred, blur_radius);

            // 2. Draw glass panel into a copy of the canvas (preserve background)
            auto glass_fb = std::make_shared<Framebuffer>(*result.framebuffer);

            RenderState current = result.state;
            if (layer.mask.enabled()) {
                current.mask = &layer.mask;
                current.layer_inv_matrix = glm::inverse(current.matrix);
            }

            for (const auto& node : layer.nodes) {
                if (!node.visible) continue;
                RenderState node_state = combine(current, node.world_transform);
                renderer::draw_glass_panel(*glass_fb, *blurred, node.shape, node_state.matrix, node_state.opacity, &node_state);
            }


            RenderPassResult glass_result;
            glass_result.framebuffer = std::move(glass_fb);
            glass_result.state = result.state;
            glass_result.has_state = result.has_state;
            return glass_result;
        });
    return add_entry(std::move(graph_node), std::move(pass));
}


NodeId RenderGraph::add_composite(std::string label,
                                  RenderCacheKey key,
                                  NodeId bottom,
                                  NodeId top,
                                  BlendMode mode) {
    auto inputs = std::vector<NodeId>{bottom, top};
    auto graph_node = std::make_unique<GraphNode>(RenderNodeKind::Composite, std::move(label), std::move(key), inputs);
    auto pass = std::make_unique<GraphPass>(
        RenderPassKind::Composite,
        std::string(graph_node->label()),
        graph_node->cache_key(),
        true,
        [bottom, top, mode](::chronon3d::rendergraph::RenderGraphExecutionContext& ctx,
                            RenderGraphExecutionState& state, NodeId) {
            RenderPassResult bottom_result;
            RenderPassResult top_result;
            if (auto it = state.results.find(bottom); it != state.results.end()) {
                bottom_result = it->second;
            }
            if (auto it = state.results.find(top); it != state.results.end()) {
                top_result = it->second;
            }

            auto fb = bottom_result.framebuffer
                ? std::make_shared<Framebuffer>(*bottom_result.framebuffer)
                : std::make_shared<Framebuffer>(ctx.width, ctx.height);

            if (!bottom_result.framebuffer) {
                fb->clear(Color::transparent());
            }
            if (top_result.framebuffer) {
                ctx.renderer.composite_layer(*fb, *top_result.framebuffer, mode);
            }

            RenderPassResult result;
            result.framebuffer = std::move(fb);
            result.state = bottom_result.state;
            result.has_state = bottom_result.has_state;
            return result;
        });
    return add_entry(std::move(graph_node), std::move(pass));
}

std::string RenderGraph::debug_dot() const {
    std::ostringstream out;
    out << "digraph RenderGraph {\n";
    out << "  rankdir=LR;\n";

    for (usize i = 0; i < m_passes.size(); ++i) {
        const auto& entry = m_passes[i];
        const auto& node = *entry.node;
        out << "  n" << i << " [label=\""
            << std::string(node.label())
            << "\\n" << static_cast<int>(node.kind())
            << "\\n" << std::hex << node.cache_key().digest() << std::dec
            << "\"];\n";
    }

    for (usize i = 0; i < m_passes.size(); ++i) {
        const auto& entry = m_passes[i];
        for (NodeId input : entry.node->inputs()) {
            out << "  n" << input << " -> n" << i << ";\n";
        }
    }

    out << "}\n";
    return out.str();
}

std::unique_ptr<Framebuffer> RenderGraph::execute(::chronon3d::rendergraph::RenderGraphExecutionContext& ctx) const {
    RenderGraphExecutionState state;
    std::shared_ptr<Framebuffer> final_fb;

    for (usize i = 0; i < m_passes.size(); ++i) {
        const auto& entry = m_passes[i];
        const auto& pass = *entry.pass;

        if (pass.cacheable()) {
            u64 key = pass.cache_key().digest();
            if (auto cached = ctx.renderer.m_node_cache.get(key)) {
                state.results[i] = RenderPassResult{.framebuffer = cached};
                final_fb = cached;
                continue;
            }
        }

        RenderPassResult result = pass.execute(ctx, state, i);
        state.results[i] = result;

        if (pass.cacheable() && result.framebuffer) {
            u64 key = pass.cache_key().digest();
            ctx.renderer.m_node_cache.put(key, result.framebuffer, result.framebuffer->size_bytes());
        }

        if (result.framebuffer) {
            final_fb = result.framebuffer;
        }
    }


    if (!final_fb) {
        final_fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        final_fb->clear(Color::transparent());
    }

    return std::make_unique<Framebuffer>(*final_fb);
}

} // namespace chronon3d::rendergraph
