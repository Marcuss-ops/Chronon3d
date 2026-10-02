#pragma once

#include <chronon3d/render_graph/nodes/basic_nodes_common.hpp>
#include <chronon3d/cache/framebuffer_copy.hpp>
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
              RenderNodeCachePolicy policy = static_memory_cache("mask"),
              bool source_surface_coordinates = false)
        : RenderGraphNode(policy), m_mask(std::move(mask)),
          m_source_surface_coordinates(source_surface_coordinates),
          m_cache_frame(cache_frame), m_text_sources(std::move(text_sources)),
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
                hash_combine(
                    hash_mask(m_mask),
                    static_cast<u64>(ctx.policy.modular_coordinates)),
                static_cast<u64>(m_source_surface_coordinates))
        };
    }

    NodeExecResult execute(RenderGraphContext& ctx, std::span<const FramebufferRef> inputs, std::span<const std::optional<raster::BBox>>) override {
        if (inputs.empty() || !inputs[0]) return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);

        auto result = ctx.acquire_owned_fb(*inputs[0]);
        if (!result) return ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height);
        if (!m_mask.enabled()) {
            return result;
        }

        // If result has a native GPU surface but no CPU data, download it for CPU raster masking
        if (!result->data() && result->surface_handle() != runtime::kInvalidRenderSurfaceHandle && ctx.backend) {
            auto cpu_fb = ctx.acquire_owned_fb(result->width(), result->height(), false);
            if (cpu_fb && cpu_fb->data()) {
                const auto scratch = ctx.download_scratch(result->width(), result->height());
                const auto dl_res = ctx.backend->download_surface(result->surface_handle(), scratch);
                if (dl_res.ok()) {
                    FramebufferCopyAuthority::copy_rows(
                        cpu_fb->pixels_row(0), cpu_fb->stride(),
                        reinterpret_cast<const Color*>(scratch.data()),
                        result->width(),
                        result->width(), result->height());
                    cpu_fb->set_origin(result->origin_x(), result->origin_y());
                    cpu_fb->set_opaque(result->is_opaque());
                    result = std::move(cpu_fb);
                }
            }
        }

        if (!result->data() || result->width() <= 0 || result->height() <= 0) {
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
                if (x < 0 || y < 0 || x >= source_fb.width() || y >= source_fb.height() || !source_fb.data())
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

        if (!m_alpha_cache || !m_alpha_cache->data()) {
            return result;
        }

        const int W = std::min(result->width(), m_alpha_cache->width());
        const int H = std::min(result->height(), m_alpha_cache->height());
        if (W <= 0 || H <= 0) {
            return result;
        }

        for (i32 y = 0; y < H; ++y) {
            Color* dst_row = result->pixels_row(y);
            const Color* mask_row = m_alpha_cache->pixels_row(y);
            if (!dst_row || !mask_row) continue;
            for (i32 x = 0; x < W; ++x) {
                const f32 m = mask_row[x].a;
                dst_row[x].r *= m;
                dst_row[x].g *= m;
                dst_row[x].b *= m;
                dst_row[x].a *= m;
            }
        }

        result->set_opaque(false);
        result->mark_cpu_authoritative();

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
        if (m_source_surface_coordinates) {
            layer_inverse[3].x = static_cast<f32>(origin_x) -
                static_cast<f32>(width) * 0.5f;
            layer_inverse[3].y = static_cast<f32>(origin_y) -
                static_cast<f32>(height) * 0.5f;
        } else {
            layer_inverse[3].x = static_cast<f32>(origin_x) - static_cast<f32>(canvas_width) * 0.5f;
            layer_inverse[3].y = static_cast<f32>(origin_y) - static_cast<f32>(canvas_height) * 0.5f;
        }
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
        seed = mix(seed, static_cast<std::uint64_t>(m_source_surface_coordinates));
        seed = mix(seed, static_cast<std::uint64_t>(m_cache_frame >= 0 ? m_cache_frame : -1));
        return seed;
    }

    Mask m_mask;
    // Video sources are masked in their decoded, zero-origin surface before
    // the layer transform. Other layer masks are interpreted in canvas space.
    bool m_source_surface_coordinates{false};
    Frame m_cache_frame{-1};
    std::vector<std::string> m_text_sources;
    Mat4 m_canvas_from_local{1.0f};
    mutable std::shared_ptr<Framebuffer> m_alpha_cache;
    mutable std::uint64_t m_alpha_cache_key{0};
};

} // namespace chronon3d::graph
