#include <chronon3d/render_graph/transition/transition_catalog.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/animation/transition/transition_progress_sampler.hpp>
#include <chronon3d/math/color.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace chronon3d::graph {

namespace {

// ── Mask generators (separated from compositing) ───────────────────────────

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
        return t * t * (3.0f - 2.0f * t);
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
        return t * t * (3.0f - 2.0f * t);
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
};

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

private:
    std::unique_ptr<LayerTransitionMask> m_mask;
};

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

                float t_alpha = 0.0f;
                if (!is_out) {
                    t_alpha = transition_math::smoothstep01(std::clamp((progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }

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

                float t_alpha = 0.0f;
                if (!is_out) {
                    t_alpha = transition_math::smoothstep01(std::clamp((progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }

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

private:
    RemotionParams m_params;
};

} // namespace

// ── Catalog ────────────────────────────────────────────────────────────────

void LayerTransitionCatalog::register_transition(std::string id, Factory factory) {
    if (id.empty()) {
        throw std::invalid_argument("LayerTransitionCatalog: transition id cannot be empty");
    }
    m_factories.emplace(std::move(id), std::move(factory));
}

bool LayerTransitionCatalog::contains(std::string_view id) const {
    return m_factories.find(std::string(id)) != m_factories.end();
}

std::unique_ptr<LayerTransitionProgram> LayerTransitionCatalog::resolve(const LayerTransitionSpec& spec) const {
    auto it = m_factories.find(spec.transition_id);
    if (it == m_factories.end()) {
        throw std::runtime_error("Unknown layer transition: " + spec.transition_id);
    }
    return it->second(spec);
}

void LayerTransitionCatalog::register_builtin(LayerTransitionCatalog& catalog) {
    catalog.register_transition("none", [](const LayerTransitionSpec&) {
        return std::make_unique<NoneProgram>();
    });
    catalog.register_transition("crossfade", [](const LayerTransitionSpec&) {
        return std::make_unique<CrossfadeProgram>();
    });
    catalog.register_transition("slide", [](const LayerTransitionSpec& spec) {
        const SlideParams* params = std::get_if<SlideParams>(&spec.parameters);
        float distance = params ? params->distance : SlideParams{}.distance;
        return std::make_unique<SlideProgram>(distance);
    });
    catalog.register_transition("wipe_linear", [](const LayerTransitionSpec&) {
        return std::make_unique<MaskBasedWipeProgram>(std::make_unique<LinearWipeMask>());
    });
    catalog.register_transition("smooth_wipe", [](const LayerTransitionSpec& spec) {
        const SmoothWipeParams* params = std::get_if<SmoothWipeParams>(&spec.parameters);
        float feather = params ? params->feather : SmoothWipeParams{}.feather;
        return std::make_unique<MaskBasedWipeProgram>(std::make_unique<SmoothWipeMask>(feather));
    });
    catalog.register_transition("circle_iris", [](const LayerTransitionSpec& spec) {
        const CircleIrisParams* params = std::get_if<CircleIrisParams>(&spec.parameters);
        Vec2 center = params ? params->center : CircleIrisParams{}.center;
        float feather = params ? params->feather : CircleIrisParams{}.feather;
        return std::make_unique<MaskBasedWipeProgram>(std::make_unique<CircleIrisMask>(center, feather));
    });
    catalog.register_transition("flash", [](const LayerTransitionSpec& spec) {
        const FlashParams* params = std::get_if<FlashParams>(&spec.parameters);
        Color color = params ? params->color : FlashParams{}.color;
        return std::make_unique<FlashProgram>(color);
    });
    catalog.register_transition("procedural_remotion", [](const LayerTransitionSpec& spec) {
        const ProceduralRemotionParams* params = std::get_if<ProceduralRemotionParams>(&spec.parameters);
        ProceduralRemotionParams resolved = params ? *params : ProceduralRemotionParams{};
        return std::make_unique<ProceduralRemotionProgram>(resolved);
    });
    catalog.register_transition("remotion", [](const LayerTransitionSpec& spec) {
        const RemotionParams* params = std::get_if<RemotionParams>(&spec.parameters);
        RemotionParams resolved = params ? *params : RemotionParams{};
        return std::make_unique<RemotionProgram>(resolved);
    });
}

} // namespace chronon3d::graph
