#pragma once

#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/rendering/shadow_settings.hpp>
#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/compositor/blend_mode.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <span>

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
        h = hash_combine(h, hash_value(m_settings.contact_opacity));
        h = hash_combine(h, hash_value(m_settings.contact_blur_radius));
        h = hash_combine(h, hash_value(m_settings.ambient_opacity));
        h = hash_combine(h, hash_value(m_settings.ambient_blur_radius));
        h = hash_combine(h, hash_value(m_settings.depth_aware));
        h = hash_combine(h, hash_value(m_settings.blur_per_z));
        h = hash_combine(h, hash_value(m_settings.opacity_falloff));
        h = hash_combine(h, hash_color(m_settings.tint));
        return cache::NodeCacheKey{
            .scope = "shadow:" + m_caster_name,
            .frame = frame_dependent() ? ctx.frame : Frame{0},
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = h
        };
    }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty() || !input_bboxes[0]) {
            return std::nullopt;
        }
        auto bbox = *input_bboxes[0];
        if (bbox.is_empty()) {
            return bbox;
        }

        const float dz = m_caster_world_z - m_receiver_world_z;
        const float depth = std::abs(dz);
        const float depth_blur_factor_b = m_settings.depth_aware ? m_settings.blur_per_z : 0.0030f;
        const float depth_blur_scale = 1.0f + depth * depth_blur_factor_b;
        const float eps = 1e-4f;
        const float safe_y = std::abs(m_light_dir.y) > eps
            ? m_light_dir.y
            : std::copysign(eps, m_light_dir.y != 0.0f ? m_light_dir.y : 1.0f);
        float ox = -(m_light_dir.x / safe_y) * dz * m_settings.px_per_unit;
        float oy = (m_light_dir.z / safe_y) * dz * m_settings.px_per_unit;
        ox = std::clamp(ox, -m_settings.max_offset, m_settings.max_offset);
        oy = std::clamp(oy, -m_settings.max_offset, m_settings.max_offset);
        const int dx = static_cast<int>(std::round(ox));
        const int dy = static_cast<int>(std::round(oy));
        const float contact_blur = std::max(1.0f, std::max(m_settings.contact_blur_radius, m_settings.blur_radius * 0.45f) * depth_blur_scale);
        const float ambient_blur = std::max(contact_blur + 4.0f, std::max(m_settings.ambient_blur_radius, m_settings.blur_radius * 2.0f) * depth_blur_scale);
        const float blur = std::max(contact_blur, ambient_blur);

        bbox.x0 += dx;
        bbox.y0 += dy;
        bbox.x1 += dx;
        bbox.y1 += dy;

        if (blur > 0.0f) {
            bbox.x0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.x0) - blur)));
            bbox.y0 = std::max(0, static_cast<i32>(std::floor(static_cast<f32>(bbox.y0) - blur)));
            bbox.x1 = std::min(ctx.width, static_cast<i32>(std::ceil(static_cast<f32>(bbox.x1) + blur)));
            bbox.y1 = std::min(ctx.height, static_cast<i32>(std::ceil(static_cast<f32>(bbox.y1) + blur)));
        } else {
            bbox.x0 = std::max(0, bbox.x0);
            bbox.y0 = std::max(0, bbox.y0);
            bbox.x1 = std::min(ctx.width, bbox.x1);
            bbox.y1 = std::min(ctx.height, bbox.y1);
        }
        return bbox;
    }

    OwnedFB execute(
        RenderGraphContext& ctx,
        std::span<const FramebufferRef> inputs,
        std::span<const std::optional<raster::BBox>>
    ) override {
        auto result = ctx.acquire_owned_fb(ctx.width, ctx.height);
        if (inputs.empty() || !inputs[0]) return result;
        const Framebuffer& src = *inputs[0];

        // Screen-space offset: dz = caster_z - receiver_z, then project along light XZ.
        const float dz = m_caster_world_z - m_receiver_world_z;
        const float depth = std::abs(dz);
        const float depth_blur_factor = m_settings.depth_aware ? m_settings.blur_per_z : 0.0030f;
        const float depth_falloff_factor = m_settings.depth_aware ? m_settings.opacity_falloff : 0.0025f;
        const float depth_blur_scale = 1.0f + depth * depth_blur_factor;
        const float depth_opacity_scale = 1.0f / (1.0f + depth * depth_falloff_factor);
        const float eps = 1e-4f;
        const float safe_y = std::abs(m_light_dir.y) > eps
            ? m_light_dir.y
            : std::copysign(eps, m_light_dir.y != 0.0f ? m_light_dir.y : 1.0f);
        float ox = -(m_light_dir.x / safe_y) * dz * m_settings.px_per_unit;
        float oy = (m_light_dir.z / safe_y) * dz * m_settings.px_per_unit;
        ox = std::clamp(ox, -m_settings.max_offset, m_settings.max_offset);
        oy = std::clamp(oy, -m_settings.max_offset, m_settings.max_offset);
        const int dx = static_cast<int>(std::round(ox));
        const int dy = static_cast<int>(std::round(oy));
        const float contact_blur = std::max(1.0f, std::max(m_settings.contact_blur_radius, m_settings.blur_radius * 0.45f) * depth_blur_scale);
        const float ambient_blur = std::max(contact_blur + 4.0f, std::max(m_settings.ambient_blur_radius, m_settings.blur_radius * 2.0f) * depth_blur_scale);

        auto render_shadow_layer = [&](f32 opacity_scale, f32 blur_radius, f32 offset_scale, Color tint) {
            const int layer_dx = static_cast<int>(std::round(static_cast<f32>(dx) * offset_scale));
            const int layer_dy = static_cast<int>(std::round(static_cast<f32>(dy) * offset_scale));
            auto shadow_fb = ctx.acquire_owned_fb(ctx.width, ctx.height);
            shadow_fb->clear({0.0f, 0.0f, 0.0f, 0.0f});

            int projected_pixels = 0;
            for (int y = 0; y < src.height(); ++y) {
                const Color* src_row = src.pixels_row(y);
                const int dst_y = y + src.origin_y() + layer_dy;
                if (dst_y < 0 || dst_y >= shadow_fb->height()) continue;
                Color* dst_row = shadow_fb->pixels_row(dst_y);
                for (int x = 0; x < src.width(); ++x) {
                    if (src_row[x].a <= 0.0f) continue;
                    const int dst_x = x + src.origin_x() + layer_dx;
                    if (dst_x < 0 || dst_x >= shadow_fb->width()) continue;
                    const float alpha = std::min(1.0f, src_row[x].a * m_settings.opacity * opacity_scale * depth_opacity_scale);
                    Color& dst = dst_row[dst_x];
                    dst.r = std::min(1.0f, dst.r + tint.r * alpha);
                    dst.g = std::min(1.0f, dst.g + tint.g * alpha);
                    dst.b = std::min(1.0f, dst.b + tint.b * alpha);
                    dst.a = std::min(1.0f, dst.a + alpha);
                    ++projected_pixels;
                }
            }

            if (blur_radius > 0.0f && ctx.backend) {
                ctx.backend->apply_blur(*shadow_fb, blur_radius, ctx.clip_rect);
            }

            spdlog::info(
                "[shadow-node] caster='{}' layer={} src_size={}x{} origin=({},{}) dx={} dy={} projected_pixels={}",
                m_caster_name,
                (offset_scale < 1.2f ? "contact" : "ambient"),
                src.width(), src.height(), src.origin_x(), src.origin_y(),
                layer_dx, layer_dy, projected_pixels
            );

            return shadow_fb;
        };

        const Color shadow_tint = m_settings.depth_aware ? m_settings.tint : Color{0.03f, 0.04f, 0.08f, 1.0f};
        auto contact = render_shadow_layer(m_settings.contact_opacity, contact_blur, 1.0f, shadow_tint);
        auto ambient = render_shadow_layer(m_settings.ambient_opacity, ambient_blur, 1.75f, shadow_tint);

        for (int y = 0; y < ctx.height; ++y) {
            Color* dst_row = result->pixels_row(y);
            const Color* contact_row = contact->pixels_row(y);
            const Color* ambient_row = ambient->pixels_row(y);
            for (int x = 0; x < ctx.width; ++x) {
                const Color mixed = compositor::blend(contact_row[x], ambient_row[x], BlendMode::Normal);
                if (mixed.a > 0.0f) {
                    dst_row[x] = compositor::blend(dst_row[x], mixed, BlendMode::Normal);
                }
            }
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
