#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/scene/layer/layer.hpp>
#include <chronon3d/scene/mask/mask_utils.hpp>
#include <spdlog/spdlog.h>

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
    SourceNode(std::string name, const ::chronon3d::RenderNode& node, const cache::NodeCacheKey& key,
               bool centered = false, bool is_3d = false, std::optional<Mat4> matrix_override = std::nullopt,
               std::optional<f32> opacity_override = std::nullopt)
        : m_name(std::move(name)), m_node(node), m_key(key), m_centered(centered), m_is_3d(is_3d), 
          m_matrix_override(matrix_override), m_opacity_override(opacity_override) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Source; }
    std::string name() const override { return m_name; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override { 
        auto key = m_key;
        key.params_hash = hash_combine(key.params_hash, static_cast<u64>(ctx.modular_coordinates));
        if (m_matrix_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_matrix_override)[0][0], sizeof(Mat4)));
        }
        if (m_opacity_override) {
            key.params_hash = hash_combine(key.params_hash, hash_bytes(&(*m_opacity_override), sizeof(f32)));
        }
        return key; 
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, const std::vector<std::shared_ptr<Framebuffer>>&) override {
        auto fb = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        fb->clear(Color::transparent());
        
        if (ctx.backend) {
            RenderState state;
            const Mat4 ssaa_scale = math::scale(Vec3(ctx.ssaa_factor, ctx.ssaa_factor, 1.0f));
            const Mat4 canvas_center = math::translate(Vec3(ctx.width * 0.5f, ctx.height * 0.5f, 0.0f));

            if (m_is_3d) {
                // Projected 3D layers: draw shapes at their local position relative to layer origin.
                // TransformNode handles layer-level projection (position, rotation, perspective).
                // Native-3D shapes (FakeBox3D etc.) ignore state.matrix and use the projector directly.
                state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
            } else {
                if (ctx.modular_coordinates) {
                    // Modular 2D also uses center-origin logic for consistency.
                    // The relative transform is handled by TransformNode.
                    state.matrix = canvas_center * ssaa_scale * m_matrix_override.value_or(m_node.world_transform.to_mat4());
                } else {
                    // Legacy path
                    Mat4 canvas_offset = ssaa_scale;
                    if (m_centered) {
                        canvas_offset = canvas_center * canvas_offset;
                    }
                    state.matrix = canvas_offset * m_node.world_transform.to_mat4();
                }
            }
            
            state.opacity = m_opacity_override.value_or(m_node.world_transform.opacity);
            
            // Expose projection context to processors that use it directly (FakeBox3D etc.).
            if (ctx.has_camera_2_5d) {
                state.projection  = ctx.projection_ctx;
                state.world_matrix = m_matrix_override.value_or(m_node.world_transform.to_mat4());
            }

            ctx.backend->draw_node(*fb, m_node, state, ctx.camera, ctx.width, ctx.height);
        }
        return fb;
    }

private:
    std::string m_name;
    ::chronon3d::RenderNode m_node;
    cache::NodeCacheKey m_key;
    bool m_centered{false};
    bool m_is_3d{false};
    std::optional<Mat4> m_matrix_override;
    std::optional<f32> m_opacity_override;
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

        f32 cx = 0.0f;
        f32 cy = 0.0f;
        if (ctx.modular_coordinates) {
            cx = static_cast<f32>(result->width()) * 0.5f;
            cy = static_cast<f32>(result->height()) * 0.5f;
        }

        for (i32 y = 0; y < result->height(); ++y) {
            Color* row = result->pixels_row(y);
            for (i32 x = 0; x < result->width(); ++x) {
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
        if (ctx.backend) {
            ctx.backend->apply_effect_stack(*result, m_effects);
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
        if (ctx.backend) {
            ctx.backend->apply_effect_stack(*result, m_effects);
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
        if (ctx.backend) {
            ctx.backend->composite_layer(*result, *top, m_mode);
        }
        return result;
    }

private:
    BlendMode m_mode;
};

} // namespace chronon3d::graph
