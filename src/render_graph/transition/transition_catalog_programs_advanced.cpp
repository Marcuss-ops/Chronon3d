#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/animation/transition/transition_progress_sampler.hpp>
#include <chronon3d/math/color.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "transition_catalog_registration_detail.hpp"

namespace chronon3d::graph {
namespace {
// Both remotion-family transitions fade the source over the same window and
// then overlay their own emission math.  Keeping the ramp here prevents the
// two shaders from drifting apart.
[[nodiscard]] float remotion_fade_alpha(float progress, bool is_out) {
    const float fade = transition_math::smoothstep01(
        std::clamp((progress - 0.2f) / 0.6f, 0.0f, 1.0f));
    return is_out ? 1.0f - fade : fade;
}

class FlashProgram final : public LayerTransitionProgram {
public:
    explicit FlashProgram(const Color& color) : m_color(color) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)ctx;
        const i32 w = src->width();
        const i32 h = src->height();

        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                Color s = src_row[x];
                Color out_color = Color::transparent();
                if (!is_out) {
                    if (progress < 0.5f) {
                        const float t = progress / 0.5f;
                        out_color = m_color * t;
                        out_color.a = s.a * t;
                    } else {
                        const float t = (progress - 0.5f) / 0.5f;
                        out_color = lerp(m_color.with_alpha(s.a), s, t);
                    }
                } else {
                    if (progress < 0.5f) {
                        const float t = progress / 0.5f;
                        out_color = lerp(s, m_color.with_alpha(s.a), t);
                    } else {
                        const float t = (progress - 0.5f) / 0.5f;
                        out_color = m_color * (1.0f - t);
                        out_color.a = s.a * (1.0f - t);
                    }
                }
                dst_row[x] = out_color;
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& /*input_bbox*/,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

private:
    Color m_color;
};

class ProceduralRemotionProgram final : public LayerTransitionProgram {
public:
    explicit ProceduralRemotionProgram(const ProceduralRemotionParams& params)
        : m_params(params) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)ctx;
        const i32 w = src->width();
        const i32 h = src->height();

        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);

                const float t_alpha = remotion_fade_alpha(progress, is_out);
                Color base = src_row[x] * t_alpha;

                float mask = transition_math::procedural_remotion_mask(u, v, progress, m_params.seed);
                float intensity = mask * 5.0f;

                Color leak = lerp(m_params.inner_color, m_params.mid_color, mask);
                leak = lerp(leak, m_params.outer_color, mask * mask * mask);

                float hole_factor = transition_math::smoothstep01(std::clamp(mask / 0.4f, 0.0f, 1.0f));
                base.r *= (1.0f - hole_factor);
                base.g *= (1.0f - hole_factor);
                base.b *= (1.0f - hole_factor);

                Color res;
                res.r = std::clamp(base.r + leak.r * intensity, 0.0f, 1.0f);
                res.g = std::clamp(base.g + leak.g * intensity, 0.0f, 1.0f);
                res.b = std::clamp(base.b + leak.b * intensity, 0.0f, 1.0f);
                res.a = std::clamp(base.a + mask * 0.8f, 0.0f, 1.0f);

                dst_row[x] = res;
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& /*input_bbox*/,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

private:
    ProceduralRemotionParams m_params;
};

class RemotionProgram final : public LayerTransitionProgram {
public:
    explicit RemotionProgram(const RemotionParams& params)
        : m_params(params) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)ctx;
        const i32 w = src->width();
        const i32 h = src->height();

        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);

                const float t_alpha = remotion_fade_alpha(progress, is_out);
                Color base = src_row[x] * t_alpha;

                float remotion_alpha = transition_math::remotion_mask(u, v, progress, m_params.speed, m_params.direction);
                float remotion_r = 0.0f, remotion_g = 0.0f, remotion_b = 0.0f;
                transition_math::evaluate_remotion_color(u, v, progress, m_params.speed, m_params.direction, m_params.angle,
                                                         remotion_r, remotion_g, remotion_b);

                Color res;
                res.r = std::clamp(base.r * (1.0f - remotion_alpha) + remotion_r * remotion_alpha, 0.0f, 1.0f);
                res.g = std::clamp(base.g * (1.0f - remotion_alpha) + remotion_g * remotion_alpha, 0.0f, 1.0f);
                res.b = std::clamp(base.b * (1.0f - remotion_alpha) + remotion_b * remotion_alpha, 0.0f, 1.0f);
                res.a = std::clamp(base.a + remotion_alpha * 0.8f, 0.0f, 1.0f);

                dst_row[x] = res;
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& /*input_bbox*/,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

private:
    RemotionParams m_params;
};

// Camera dip handoff for chain segments: the incoming layer starts `dip_px`
// pixels below its resting position (fully transparent) and rises into place
// while fading in; the exit variant mirrors it downwards. Mirrors the
// `chain_camera: fly_under_fade` contract stamped by RenderingGen chain items.
class FlyUnderFadeProgram final : public LayerTransitionProgram {
public:
    explicit FlyUnderFadeProgram(float dip_px) : m_dip_px(std::max(0.0f, dip_px)) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)ctx;
        const float p = std::clamp(progress, 0.0f, 1.0f);
        const int dip = dip_offset(p, is_out);
        const float alpha = is_out ? (1.0f - p) : p;
        const i32 w = src->width();
        const i32 h = src->height();
        for (i32 y = 0; y < h; ++y) {
            Color* dst_row = out.pixels_row(y);
            const i32 src_y = y - dip;
            for (i32 x = 0; x < w; ++x) {
                if (src_y < 0 || src_y >= h) {
                    dst_row[x] = Color::transparent();
                } else {
                    dst_row[x] = src->get_pixel(x, src_y) * alpha;
                }
            }
        }
    }

    [[nodiscard]] NativeBlendParams native_blend_params(float progress, bool is_out) const override {
        const float p = std::clamp(progress, 0.0f, 1.0f);
        NativeBlendParams params;
        params.offset_x = 0;
        params.offset_y = dip_offset(p, is_out);
        params.opacity = is_out ? (1.0f - p) : p;
        params.supported = true;
        return params;
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>&,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

private:
    float m_dip_px;

    [[nodiscard]] int dip_offset(float p, bool is_out) const {
        const float travel = is_out ? p : (1.0f - p);
        return static_cast<int>(std::lround(m_dip_px * travel));
    }
};


} // namespace

std::unique_ptr<LayerTransitionProgram> make_flash(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<FlashParams>(&spec.parameters);
    return std::make_unique<FlashProgram>(params ? params->color : FlashParams{}.color);
}

std::unique_ptr<LayerTransitionProgram> make_procedural_remotion(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<ProceduralRemotionParams>(&spec.parameters);
    return std::make_unique<ProceduralRemotionProgram>(params ? *params : ProceduralRemotionParams{});
}

std::unique_ptr<LayerTransitionProgram> make_remotion(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<RemotionParams>(&spec.parameters);
    return std::make_unique<RemotionProgram>(params ? *params : RemotionParams{});
}

std::unique_ptr<LayerTransitionProgram> make_fly_under_fade(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<FlyUnderFadeParams>(&spec.parameters);
    return std::make_unique<FlyUnderFadeProgram>(params ? params->dip_px : FlyUnderFadeParams{}.dip_px);
}


} // namespace chronon3d::graph
