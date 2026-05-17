#pragma once

#include <chronon3d/render_graph/render_graph_node.hpp>
#include <chronon3d/render_graph/render_graph_hashing.hpp>
#include <chronon3d/rendering/shadow_settings.hpp>
#include <chronon3d/math/vec3.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace chronon3d::graph {

// Projects the alpha silhouette of a caster layer onto a receiver layer.
// Input[0]: caster transformed framebuffer.
// Output:   transparent-black framebuffer at the shadow offset position.
class ShadowNode final : public RenderGraphNode {
public:
    ShadowNode(std::string caster_name,
               f32 caster_world_z,
               f32 receiver_world_z,
               Vec3 light_direction,
               rendering::ShadowSettings settings)
        : m_caster_name(std::move(caster_name))
        , m_caster_world_z(caster_world_z)
        , m_receiver_world_z(receiver_world_z)
        , m_light_dir(light_direction)
        , m_settings(settings) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Effect; }
    std::string name() const override { return "Shadow"; }
    [[nodiscard]] bool cacheable() const override { return true; }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        u64 h = hash_string(m_caster_name);
        h = hash_combine(h, hash_value(m_caster_world_z));
        h = hash_combine(h, hash_value(m_receiver_world_z));
        h = hash_combine(h, hash_vec3(m_light_dir));
        h = hash_combine(h, hash_value(m_settings.opacity));
        h = hash_combine(h, hash_value(m_settings.blur_radius));
        h = hash_combine(h, hash_value(m_settings.px_per_unit));
        h = hash_combine(h, hash_value(m_settings.max_offset));
        return cache::NodeCacheKey{
            .scope = "shadow:" + m_caster_name,
            .frame = ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = h
        };
    }

    std::shared_ptr<Framebuffer> execute(
        RenderGraphContext& ctx,
        const std::vector<std::shared_ptr<Framebuffer>>& inputs) override
    {
        auto result = std::make_shared<Framebuffer>(ctx.width, ctx.height);
        result->clear(Color::transparent());

        if (inputs.empty() || !inputs[0]) return result;
        const Framebuffer& src = *inputs[0];

        // Screen-space offset: dz = caster_z - receiver_z, then project along light XZ
        const float dz = m_caster_world_z - m_receiver_world_z;
        const float eps = 1e-4f;
        const float safe_y = std::abs(m_light_dir.y) > eps
            ? m_light_dir.y
            : std::copysign(eps, m_light_dir.y != 0.0f ? m_light_dir.y : 1.0f);
        float ox = -(m_light_dir.x / safe_y) * dz * m_settings.px_per_unit;
        float oy = -(m_light_dir.z / safe_y) * dz * m_settings.px_per_unit;
        ox = std::clamp(ox, -m_settings.max_offset, m_settings.max_offset);
        oy = std::clamp(oy, -m_settings.max_offset, m_settings.max_offset);
        const int dx = static_cast<int>(std::round(ox));
        const int dy = static_cast<int>(std::round(oy));

        // Translate caster alpha → opaque-black shadow pixels at (x+dx, y+dy)
        for (int y = 0; y < src.height(); ++y) {
            const Color* src_row = src.pixels_row(y);
            const int dst_y = y + dy;
            if (dst_y < 0 || dst_y >= result->height()) continue;
            Color* dst_row = result->pixels_row(dst_y);
            for (int x = 0; x < src.width(); ++x) {
                if (src_row[x].a <= 0.0f) continue;
                const int dst_x = x + dx;
                if (dst_x < 0 || dst_x >= result->width()) continue;
                const float alpha = std::min(1.0f, src_row[x].a * m_settings.opacity);
                dst_row[dst_x].a = std::min(1.0f, dst_row[dst_x].a + alpha);
            }
        }

        if (m_settings.blur_radius > 0.0f && ctx.backend) {
            ctx.backend->apply_blur(*result, m_settings.blur_radius);
        }

        return result;
    }

    static std::unique_ptr<ShadowNode> create(std::string caster_name,
                                               f32 caster_world_z,
                                               f32 receiver_world_z,
                                               Vec3 light_direction,
                                               rendering::ShadowSettings settings)
    {
        return std::make_unique<ShadowNode>(
            std::move(caster_name), caster_world_z, receiver_world_z,
            light_direction, settings);
    }

private:
    std::string m_caster_name;
    f32 m_caster_world_z;
    f32 m_receiver_world_z;
    Vec3 m_light_dir;
    rendering::ShadowSettings m_settings;
};

} // namespace chronon3d::graph
