#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>

namespace chronon3d::graph {

class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask, Frame cache_frame = Frame{-1})
        : m_mask(std::move(mask)), m_cache_frame(cache_frame) {}

    RenderGraphNodeKind kind() const override { return RenderGraphNodeKind::Mask; }
    std::string name() const override { return "Mask"; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext&,
        std::span<const std::optional<raster::BBox>> input_bboxes = {}
    ) const override {
        if (input_bboxes.empty()) return std::nullopt;
        return input_bboxes[0];
    }

    [[nodiscard]] CacheFramePolicy cache_frame_policy() const override {
        return CacheFramePolicy::FrameInvariant;
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext& ctx) const override {
        return cache::NodeCacheKey{
            .scope = "mask",
            .frame = m_cache_frame >= 0 ? m_cache_frame : ctx.frame,
            .width = ctx.width,
            .height = ctx.height,
            .params_hash = hash_mask(m_mask)
        };
    }

    std::shared_ptr<Framebuffer> execute(RenderGraphContext& ctx, std::span<const std::shared_ptr<Framebuffer>> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty()) return ctx.acquire_framebuffer(ctx.width, ctx.height);

        auto result = ctx.acquire_framebuffer(*inputs[0]);
        if (!m_mask.enabled()) {
            return result;
        }

        const auto cache_key = mask_alpha_cache_key(result->width(), result->height(), ctx.modular_coordinates);
        if (!m_alpha_cache || m_alpha_cache_key != cache_key) {
            m_alpha_cache = build_alpha_cache(result->width(), result->height(), ctx.modular_coordinates);
            m_alpha_cache_key = cache_key;
        }

        if (!m_alpha_cache) {
            return result;
        }

        const int W = result->width();
        const int H = result->height();
        if (W >= 100 && H >= 100) {
            Color p50_before = result->get_pixel(50, 50);
            Color p30_before = result->get_pixel(30, 50);
            Color m50 = m_alpha_cache->get_pixel(50, 50);
            Color m30 = m_alpha_cache->get_pixel(30, 50);
            spdlog::info("DEBUG MASK EXECUTE BEFORE: p50.a={} p30.a={} m50.a={} m30.a={} modular={}",
                p50_before.a, p30_before.a, m50.a, m30.a, ctx.modular_coordinates);
        }

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

        if (W >= 100 && H >= 100) {
            Color p50_after = result->get_pixel(50, 50);
            Color p30_after = result->get_pixel(30, 50);
            spdlog::info("DEBUG MASK EXECUTE AFTER: p50.a={} p30.a={}",
                p50_after.a, p30_after.a);
        }

        return result;
    }

private:
    [[nodiscard]] std::shared_ptr<Framebuffer> build_alpha_cache(i32 width, i32 height, bool modular_coordinates) const {
        if (!modular_coordinates) {
            return rasterize_mask_alpha(m_mask, Mat4{1.0f}, width, height);
        }

        auto fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color::transparent());

        const f32 cx = static_cast<f32>(width) * 0.5f;
        const f32 cy = static_cast<f32>(height) * 0.5f;
        int count = 0;
        for (i32 y = 0; y < height; ++y) {
            Color* row = fb->pixels_row(y);
            for (i32 x = 0; x < width; ++x) {
                if (mask_contains_local_point(m_mask, Vec2{static_cast<f32>(x) - cx, static_cast<f32>(y) - cy})) {
                    row[x] = {1.0f, 1.0f, 1.0f, 1.0f};
                    count++;
                }
            }
        }
        spdlog::info("DEBUG BUILD ALPHA CACHE: width={} height={} cx={} cy={} count={} mask_enabled={} mask_type={}",
            width, height, cx, cy, count, m_mask.enabled(), static_cast<int>(m_mask.type));
        return fb;
    }

    [[nodiscard]] std::uint64_t mask_alpha_cache_key(i32 width, i32 height, bool modular_coordinates) const {
        auto mix = [](std::uint64_t seed, std::uint64_t value) {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        };

        std::uint64_t seed = hash_mask(m_mask);
        seed = mix(seed, static_cast<std::uint64_t>(width));
        seed = mix(seed, static_cast<std::uint64_t>(height));
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
