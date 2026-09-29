#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <span>
#include <algorithm>
#include <string>
#include <vector>

namespace chronon3d::graph {

class MaskNode final : public RenderGraphNode {
public:
    MaskNode(Mask mask,
              Frame cache_frame = Frame{-1},
              std::vector<std::string> text_sources = {},
              Mat4 canvas_from_local = Mat4{1.0f},
              RenderNodeCachePolicy policy = static_memory_cache("mask"))
        : RenderGraphNode(policy), m_mask(std::move(mask)), m_cache_frame(cache_frame),
          m_text_sources(std::move(text_sources)),
          m_canvas_from_local(std::move(canvas_from_local)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Mask; }
    std::string_view name() const noexcept override { return "Mask"; }
    bool has_compiled_recorder() const noexcept override { return true; }

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
            .frame = cache_frame_for_policy(
                cache_policy(), ctx.frame_input.frame, m_cache_frame),
            .width = ctx.frame_input.width,
            .height = ctx.frame_input.height,
            .params_hash = hash_combine(
                hash_mask(m_mask),
                static_cast<u64>(ctx.policy.modular_coordinates))
        };
    }

    NodeExecResult execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty()) return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);

        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (!m_mask.enabled()) {
            return result;
        }

        const bool has_text_source = !m_text_sources.empty();
        const auto cache_key = mask_alpha_cache_key(
            result->width(),
            result->height(),
            result->origin_x(),
            result->origin_y(),
            ctx.frame_input.width,
            ctx.frame_input.height,
            ctx.policy.modular_coordinates
        );
        if (has_text_source || !m_alpha_cache || m_alpha_cache_key != cache_key) {
            const TextMaskSampler sample_text = [&](std::string_view source, Vec2 pixel) {
                const auto found = std::find(m_text_sources.begin(), m_text_sources.end(), source);
                if (found == m_text_sources.end()) return 0.0f;
                const auto input_index = static_cast<std::size_t>(found - m_text_sources.begin()) + 1;
                if (input_index >= inputs.size() || !inputs[input_index]) return 0.0f;
                const auto& source_fb = *inputs[input_index];
                const Vec4 canvas = m_canvas_from_local * Vec4{pixel.x, pixel.y, 0.0f, 1.0f};
                if (std::abs(canvas.w) <= 1e-7f) return 0.0f;
                const i32 x = static_cast<i32>(std::floor(canvas.x / canvas.w)) - source_fb.origin_x();
                const i32 y = static_cast<i32>(std::floor(canvas.y / canvas.w)) - source_fb.origin_y();
                if (x < 0 || y < 0 || x >= source_fb.width() || y >= source_fb.height())
                    return 0.0f;
                return std::clamp(source_fb.pixels_row(y)[x].a, 0.0f, 1.0f);
            };
            m_alpha_cache = build_alpha_cache(
                result->width(),
                result->height(),
                result->origin_x(),
                result->origin_y(),
                ctx.frame_input.width,
                ctx.frame_input.height,
                ctx.policy.modular_coordinates,
                has_text_source ? &sample_text : nullptr
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
        bool modular_coordinates,
        const TextMaskSampler* text_sampler
    ) const {
        if (!modular_coordinates) {
            return text_sampler
                ? rasterize_mask_alpha(m_mask, Mat4{1.0f}, width, height,
                    origin_x, origin_y, *text_sampler)
                : rasterize_mask_alpha(m_mask, Mat4{1.0f}, width, height);
        }
        Mat4 layer_inverse{1.0f};
        layer_inverse[3].x = static_cast<f32>(origin_x) - static_cast<f32>(canvas_width) * 0.5f;
        layer_inverse[3].y = static_cast<f32>(origin_y) - static_cast<f32>(canvas_height) * 0.5f;
        return text_sampler
            ? rasterize_mask_alpha(m_mask, layer_inverse, width, height,
                origin_x, origin_y, *text_sampler)
            : rasterize_mask_alpha(m_mask, layer_inverse, width, height);
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
    std::vector<std::string> m_text_sources;
    Mat4 m_canvas_from_local{1.0f};
    mutable std::shared_ptr<Framebuffer> m_alpha_cache;
    mutable std::uint64_t m_alpha_cache_key{0};
};

} // namespace chronon3d::graph
