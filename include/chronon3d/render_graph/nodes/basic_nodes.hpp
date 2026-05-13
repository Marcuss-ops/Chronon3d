#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/scene/layer.hpp>
#include <chronon3d/scene/mask_utils.hpp>

namespace chronon3d::graph {

// ClearNode (Output/Start)
class ClearNode final : public RenderGraphNode {
public:
    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Output; }
    std::string name() const override { return "Clear"; }

    bool cacheable() const override { return false; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "clear",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>&) override {
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        return fb;
    }
};

// SourceNode
class SourceNode final : public RenderGraphNode {
public:
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key)
        : m_name(std::move(name)), m_node(node), m_key(key) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override { 
        auto key = m_key;
        // In the modular system, the SourceNode might depend on its own source hash
        // which we already have in m_key.source_hash.
        return key; 
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>&) override {
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        
        // Root centering: scene (0,0) -> pixel (width/2, height/2)
        Mat4 canvas_offset = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));
        RenderState state{canvas_offset, 1.0f};

        // Combine with node's own world transform
        state = combine(state, m_node.world_transform);
        
        if (ctx.renderer) {
            ctx.renderer->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);
        }
        return fb;
    }

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node; // Copied for safety
    cache::NodeCacheKey m_key;
};

// MaskNode
class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask) : m_mask(std::move(mask)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Mask; }
    std::string name() const override { return "Mask"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mask",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_mask(m_mask)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) return std::make_shared<Framebuffer>(ctx.width, ctx.height);

        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        const f32 cx = ctx.width  * 0.5f;
        const f32 cy = ctx.height * 0.5f;
        for (i32 y = 0; y < ctx.height; ++y) {
            Color* row = result->pixels_row(y);
            for (i32 x = 0; x < ctx.width; ++x) {
                // Convert screen (0,0 = top-left) to canvas-centered local coords
                Vec2 local{static_cast<f32>(x) - cx, static_cast<f32>(y) - cy};
                if (!mask_contains_local_point(m_mask, local))
                    row[x].a = 0.0f;
            }
        }
        return result;
    }

private:
    Mask m_mask;
};

// EffectStackNode
class EffectStackNode final : public RenderGraphNode {
public:
    explicit EffectStackNode(EffectStack effects) : m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "EffectStack"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "effect_stack",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        
        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        if (ctx.renderer) {
            ctx.renderer->apply_effect_stack(*result, m_effects);
        }
        return result;
    }

private:
    EffectStack m_effects;
};

// AdjustmentNode
class AdjustmentNode final : public RenderGraphNode {
public:
    explicit AdjustmentNode(EffectStack effects) : m_effects(std::move(effects)) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Adjustment; }
    std::string name() const override { return "Adjustment"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "adjustment",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_effect_stack(m_effects)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.empty()) return std::make_shared<Framebuffer>(ctx.width, ctx.height);
        
        auto result = std::make_shared<Framebuffer>(*inputs[0]);
        if (ctx.renderer) {
            ctx.renderer->apply_effect_stack(*result, m_effects);
        }
        return result;
    }

private:
    EffectStack m_effects;
};

// CompositeNode
class CompositeNode final : public RenderGraphNode {
public:
    CompositeNode(BlendMode mode) : m_mode(mode) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Composite; }
    std::string name() const override { return "Composite"; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "composite",
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = static_cast<u64>(m_mode)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>& inputs) override {
        if (inputs.size() < 2) return inputs.empty() ? std::make_shared<Framebuffer>(ctx.width, ctx.height) : inputs[0];
        
        auto bottom = inputs[0];
        auto top = inputs[1];
        
        auto result = std::make_shared<Framebuffer>(*bottom);
        if (ctx.renderer) {
            ctx.renderer->composite_layer(*result, *top, m_mode);
        }
        return result;
    }

private:
    BlendMode m_mode;
};

} // namespace chronon3d::graph
