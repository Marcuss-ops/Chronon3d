#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/renderer/software_renderer.hpp>
#include <chronon3d/scene/layer.hpp>

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
        
        RenderState state{Mat4(1.0f), 1.0f};
        
        if (ctx.renderer) {
            ctx.renderer->draw_node(*fb, m_node, state, ctx.renderer->camera(), ctx.width, ctx.height);
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
        // Basic masking implementation could be added here if SoftwareRenderer supports it post-process
        // For now it remains a placeholder but with correct hashing.
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
