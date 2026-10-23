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
class LinearWipeMask final : public LayerTransitionMask {
public:
    [[nodiscard]] float evaluate(float u, float v, float progress, bool is_out) const override {
        // direction is resolved by the caller through u/v selection.
        (void)is_out;
        (void)progress;
        float val = u;
        if (is_out) {
            return (val >= progress) ? 1.0f : 0.0f;
        }
        return (val <= progress) ? 1.0f : 0.0f;
    }
};

class SmoothWipeMask final : public LayerTransitionMask {
public:
    explicit SmoothWipeMask(float feather) : m_feather(feather) {}

    [[nodiscard]] float evaluate(float u, float v, float progress, bool is_out) const override {
        (void)v;
        const float sweep = progress * (1.0f + m_feather);
        float t = 0.0f;
        if (!is_out) {
            t = std::clamp((sweep - u) / m_feather, 0.0f, 1.0f);
        } else {
            t = std::clamp((u - sweep + m_feather) / m_feather, 0.0f, 1.0f);
        }
        return chronon3d::math::smoothstep(t);
    }

private:
    float m_feather;
};

class CircleIrisMask final : public LayerTransitionMask {
public:
    CircleIrisMask(const Vec2& center, float feather)
        : m_center(center), m_feather(feather) {}

    [[nodiscard]] float evaluate(float u, float v, float progress, bool is_out) const override {
        const float dx = u - m_center.x;
        const float dy = v - m_center.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        // Farthest corner from the centre (normalised frame is [0,1] x [0,1]).
        const float max_r_x = std::max(m_center.x, 1.0f - m_center.x);
        const float max_r_y = std::max(m_center.y, 1.0f - m_center.y);
        const float max_r = std::sqrt(max_r_x * max_r_x + max_r_y * max_r_y);
        const float r_feather = std::max(0.001f, max_r * m_feather);
        const float sweep_r = progress * (max_r + r_feather);

        float t = 0.0f;
        if (!is_out) {
            t = std::clamp((sweep_r - dist) / r_feather, 0.0f, 1.0f);
        } else {
            t = std::clamp((max_r - sweep_r - dist + r_feather) / r_feather, 0.0f, 1.0f);
        }
        return chronon3d::math::smoothstep(t);
    }

private:
    Vec2 m_center;
    float m_feather;
};

// ── Direction helpers ───────────────────────────────────────────────────────

float select_uv(TransitionDirection direction, float u, float v) {
    switch (direction) {
        case TransitionDirection::Right: return u;
        case TransitionDirection::Left:  return 1.0f - u;
        case TransitionDirection::Down:  return v;
        case TransitionDirection::Up:    return 1.0f - v;
        default:                         return u;
    }
}

// ── Built-in programs ───────────────────────────────────────────────────────

class NoneProgram final : public LayerTransitionProgram {
public:
    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)progress; (void)direction; (void)is_out; (void)ctx;
        out = *src;
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& input_bbox,
        const RenderGraphContext& ctx
    ) const override {
        (void)ctx;
        return input_bbox;
    }
};

// Camera dip handoff for chain segments happens in FlyUnderFadeProgram below;
// crossfade is the plain alpha blend that native GPU frames accelerate with.
class CrossfadeProgram final : public LayerTransitionProgram {
public:
    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)is_out; (void)ctx;
        const float t = is_out ? (1.0f - progress) : progress;
        const i32 w = src->width();
        const i32 h = src->height();
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                dst_row[x] = src_row[x] * t;
            }
        }
    }

    [[nodiscard]] NativeBlendParams native_blend_params(float progress, bool is_out) const override {
        const float p = std::clamp(progress, 0.0f, 1.0f);
        NativeBlendParams params;
        params.offset_x = 0;
        params.offset_y = 0;
        params.opacity = is_out ? (1.0f - p) : p;
        params.supported = true;
        return params;
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& input_bbox,
        const RenderGraphContext& ctx
    ) const override {
        (void)ctx;
        return input_bbox;
    }
};

// Dip-to-black: unlike crossfade (premultiplied-alpha fade), RGB is scaled
// toward black while alpha is preserved, so the layer dips through opaque
// black instead of dissolving. `dip_px` holds full black for a fraction of
// the duration proportional to dip_px / 4096 (0 = pure eased fade).
class DipToBlackProgram final : public LayerTransitionProgram {
public:
    explicit DipToBlackProgram(float dip_px) : m_dip_px(dip_px) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)direction; (void)ctx;
        const float t = std::clamp(is_out ? (1.0f - progress) : progress, 0.0f, 1.0f);
        const float hold = std::clamp(m_dip_px / 4096.0f, 0.0f, 1.0f) * 0.5f;
        float envelope = t;
        if (hold > 0.0f) {
            // Compress the fade ramps to [0, 0.5 - hold] and [0.5 + hold, 1],
            // holding full black between them.
            if (t < 0.5f - hold) envelope = t / std::max(1e-6f, 0.5f - hold) * 0.5f;
            else if (t > 0.5f + hold) envelope = 0.5f + (t - 0.5f - hold) / std::max(1e-6f, 0.5f - hold) * 0.5f;
            else envelope = 0.0f;
        }
        const i32 w = src->width();
        const i32 h = src->height();
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                const Color& c = src_row[x];
                Color res;
                res.r = c.r * envelope;
                res.g = c.g * envelope;
                res.b = c.b * envelope;
                res.a = c.a;
                dst_row[x] = res;
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& input_bbox,
        const RenderGraphContext& ctx
    ) const override {
        (void)ctx;
        return input_bbox;
    }

private:
    float m_dip_px;
};

class SlideProgram final : public LayerTransitionProgram {
public:
    explicit SlideProgram(float distance) : m_distance(distance) {}

private:
    float m_distance;

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)ctx;
        const i32 w = src->width();
        const i32 h = src->height();

        // default distance = full dimension
        i32 dx_from = 0, dy_from = 0;
        i32 dx_to = 0, dy_to = 0;

        const float d = progress * m_distance;
        if (direction == TransitionDirection::Left) {
            dx_from = -static_cast<i32>(std::round(w * d));
            dx_to = static_cast<i32>(std::round(w * (1.0f - d)));
        } else if (direction == TransitionDirection::Right) {
            dx_from = static_cast<i32>(std::round(w * d));
            dx_to = -static_cast<i32>(std::round(w * (1.0f - d)));
        } else if (direction == TransitionDirection::Up) {
            dy_from = -static_cast<i32>(std::round(h * d));
            dy_to = static_cast<i32>(std::round(h * (1.0f - d)));
        } else if (direction == TransitionDirection::Down) {
            dy_from = static_cast<i32>(std::round(h * d));
            dy_to = -static_cast<i32>(std::round(h * (1.0f - d)));
        } else {
            dx_from = -static_cast<i32>(std::round(w * d));
            dx_to = static_cast<i32>(std::round(w * (1.0f - d)));
        }

        // During an in-transition the image comes from off-screen (dx_to),
        // during an out-transition it moves off-screen (dx_from).
        const i32 dx = is_out ? dx_from : -dx_to;
        const i32 dy = is_out ? dy_from : -dy_to;

        for (i32 y = 0; y < h; ++y) {
            Color* dst_row = out.pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                i32 src_x = x - dx;
                i32 src_y = y - dy;
                if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h) {
                    dst_row[x] = src->get_pixel(src_x, src_y);
                } else {
                    dst_row[x] = Color::transparent();
                }
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>& /*input_bbox*/,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }
};

class ZoomProgram final : public LayerTransitionProgram {
public:
    explicit ZoomProgram(float amount) : m_amount(amount) {}

    void execute(
        const FramebufferRef& src, Framebuffer& out, float progress,
        TransitionDirection direction, bool is_out, const RenderGraphContext& ctx
    ) const override {
        (void)direction;
        (void)ctx;
        const i32 width = src->width();
        const i32 height = src->height();
        const float t = std::clamp(progress, 0.0f, 1.0f);
        const float zoom_t = is_out ? t : 1.0f - t;
        const float scale = 1.0f + m_amount * zoom_t;
        const float opacity = is_out ? 1.0f - t : t;
        const float center_x = static_cast<float>(width - 1) * 0.5f;
        const float center_y = static_cast<float>(height - 1) * 0.5f;

        for (i32 y = 0; y < height; ++y) {
            Color* dst = out.pixels_row(y);
            const float source_y = std::clamp((static_cast<float>(y) - center_y) / scale + center_y,
                                              0.0f, static_cast<float>(height - 1));
            const i32 y0 = static_cast<i32>(source_y);
            const i32 y1 = std::min(y0 + 1, height - 1);
            const float fy = source_y - static_cast<float>(y0);
            const Color* row0 = src->pixels_row(y0);
            const Color* row1 = src->pixels_row(y1);
            for (i32 x = 0; x < width; ++x) {
                const float source_x = std::clamp((static_cast<float>(x) - center_x) / scale + center_x,
                                                  0.0f, static_cast<float>(width - 1));
                const i32 x0 = static_cast<i32>(source_x);
                const i32 x1 = std::min(x0 + 1, width - 1);
                const float fx = source_x - static_cast<float>(x0);
                const Color top = lerp(row0[x0], row0[x1], fx);
                const Color bottom = lerp(row1[x0], row1[x1], fx);
                dst[x] = lerp(top, bottom, fy) * opacity;
            }
        }
    }

    [[nodiscard]] std::optional<raster::BBox> predicted_bbox(
        const std::optional<raster::BBox>&,
        const RenderGraphContext& ctx
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

private:
    float m_amount;
};

class MaskBasedWipeProgram final : public LayerTransitionProgram {
public:
    MaskBasedWipeProgram(std::unique_ptr<LayerTransitionMask> mask)
        : m_mask(std::move(mask)) {}

    void execute(
        const FramebufferRef& src,
        Framebuffer& out,
        float progress,
        TransitionDirection direction,
        bool is_out,
        const RenderGraphContext& ctx
    ) const override {
        (void)ctx;
        const i32 w = src->width();
        const i32 h = src->height();
        const float inv_w = 1.0f / std::max(1, w);
        const float inv_h = 1.0f / std::max(1, h);

        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out.pixels_row(y);
            const float v = static_cast<float>(y) * inv_h;
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) * inv_w;
                const float mask_u = select_uv(direction, u, v);
                const float mask_val = m_mask->evaluate(mask_u, v, progress, is_out);
                dst_row[x] = src_row[x] * mask_val;
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
    std::unique_ptr<LayerTransitionMask> m_mask;
};


} // namespace

std::unique_ptr<LayerTransitionProgram> make_none(const LayerTransitionSpec& spec) {
    (void)spec;
    return std::make_unique<NoneProgram>();
}

std::unique_ptr<LayerTransitionProgram> make_crossfade(const LayerTransitionSpec& spec) {
    (void)spec;
    return std::make_unique<CrossfadeProgram>();
}

std::unique_ptr<LayerTransitionProgram> make_dip_to_black(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<DipToBlackParams>(&spec.parameters);
    return std::make_unique<DipToBlackProgram>(params ? params->dip_px : DipToBlackParams{}.dip_px);
}

std::unique_ptr<LayerTransitionProgram> make_slide(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<SlideParams>(&spec.parameters);
    return std::make_unique<SlideProgram>(params ? params->distance : SlideParams{}.distance);
}

std::unique_ptr<LayerTransitionProgram> make_zoom(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<ZoomParams>(&spec.parameters);
    return std::make_unique<ZoomProgram>(params ? params->amount : ZoomParams{}.amount);
}

std::unique_ptr<LayerTransitionProgram> make_wipe_linear(const LayerTransitionSpec& spec) {
    (void)spec;
    return std::make_unique<MaskBasedWipeProgram>(std::make_unique<LinearWipeMask>());
}

std::unique_ptr<LayerTransitionProgram> make_smooth_wipe(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<SmoothWipeParams>(&spec.parameters);
    return std::make_unique<MaskBasedWipeProgram>(std::make_unique<SmoothWipeMask>(params ? params->feather : SmoothWipeParams{}.feather));
}

std::unique_ptr<LayerTransitionProgram> make_circle_iris(const LayerTransitionSpec& spec) {
    const auto* params = std::get_if<CircleIrisParams>(&spec.parameters);
    const auto center = params ? params->center : CircleIrisParams{}.center;
    const float feather = params ? params->feather : CircleIrisParams{}.feather;
    return std::make_unique<MaskBasedWipeProgram>(std::make_unique<CircleIrisMask>(center, feather));
}


} // namespace chronon3d::graph
