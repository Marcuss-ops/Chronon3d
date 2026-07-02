#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask,
              Frame cache_frame = Frame{-1},
              RenderNodeCachePolicy policy = static_memory_cache("mask"))
        : RenderGraphNode(policy), m_mask(std::move(mask)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Mask; }
    std::string_view name() const noexcept override { return "Mask"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        return input_bboxes[0];
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mask",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame_input.frame,
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_mask(m_mask)
        };
    }

    NodeExecResult execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty()) return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);

        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (!m_mask.enabled()) {
            return result;
        }

        const auto cache_key = mask_alpha_cache_key(
            result->width(),
            result->height(),
            result->origin_x(),
            result->origin_y(),
            ctx.frame_input.width,
            ctx.frame_input.height,
            ctx.policy.modular_coordinates
        );
        if (!m_alpha_cache || m_alpha_cache_key != cache_key) {
            m_alpha_cache = build_alpha_cache(
                result->width(),
                result->height(),
                result->origin_x(),
                result->origin_y(),
                ctx.frame_input.width,
                ctx.frame_input.height,
                ctx.policy.modular_coordinates
            );
            m_alpha_cache_key = cache_key;
        }

        if (!m_alpha_cache) {
            return result;
        }

        const int W = result->width();
        const int H = result->height();

        for (i32 y = 0; y < H; ++y) {
            Color* dst_row = result->pixels_row(y);
            const Color* mask_row = m_alpha_cache->pixels_row(y);
            for (i32 x = 0; x < W; ++x) {
                const f32 m = mask_row[x].a;
                dst_row[x].r *= m;
                dst_row[x].g *= m;
                dst_row[x].b *= m;
                dst_row[x].a *= m;
            }
        }

        return result;
    }

private:
    [[nodiscard]] std::shared_ptr<Framebuffer> build_alpha_cache(
        i32 width,
        i32 height,
        i32 origin_x,
        i32 origin_y,
        i32 canvas_width,
        i32 canvas_height,
        bool modular_coordinates
    ) const {
        if (!modular_coordinates) {
            return rasterize_mask_alpha(m_mask, Mat4{1.0f}, width, height);
        }

        auto fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color::transparent());

        const f32 cx = modular_coordinates ? static_cast<f32>(canvas_width) * 0.5f : 0.0f;
        const f32 cy = modular_coordinates ? static_cast<f32>(canvas_height) * 0.5f : 0.0f;

        for (i32 y = 0; y < height; ++y) {
            Color* row = fb->pixels_row(y);
            const f32 global_y = static_cast<f32>(origin_y + y);
            for (i32 x = 0; x < width; ++x) {
                const f32 global_x = static_cast<f32>(origin_x + x);
                Vec2 local{global_x - cx, global_y - cy};
                if (mask_contains_local_point(m_mask, local)) {
                    row[x] = {1.0f, 1.0f, 1.0f, 1.0f};
                }
            }
        }

        return fb;
    }

    [[nodiscard]] std::uint64_t mask_alpha_cache_key(
        i32 width,
        i32 height,
        i32 origin_x,
        i32 origin_y,
        i32 canvas_width,
        i32 canvas_height,
        bool modular_coordinates
    ) const {
        auto mix = [](std::uint64_t seed, std::uint64_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        };

        std::uint64_t seed = hash_mask(m_mask);
        seed = mix(seed, static_cast<std::uint64_t>(width));
        seed = mix(seed, static_cast<std::uint64_t>(height));
        seed = mix(seed, static_cast<std::uint64_t>(origin_x));
        seed = mix(seed, static_cast<std::uint64_t>(origin_y));
        seed = mix(seed, static_cast<std::uint64_t>(canvas_width));
        seed = mix(seed, static_cast<std::uint64_t>(canvas_height));
        seed = mix(seed, static_cast<std::uint64_t>(modular_coordinates));
        seed = mix(seed, static_cast<std::uint64_t>(m_cache_frame >= 0 ? m_cache_frame : -1));
        return seed;
    }

    Mask m_mask;
    Frame m_cache_frame{-1};
    mutable std::shared_ptr<Framebuffer> m_alpha_cache;
    mutable std::uint64_t m_alpha_cache_key{0};
};

} // namespace chronon3d::graph
