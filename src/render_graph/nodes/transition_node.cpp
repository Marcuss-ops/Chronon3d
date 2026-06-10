#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <cmath>
#include <algorithm>
#include <span>
#include <limits>

namespace chronon3d::graph {

// Math helpers live in transition/transition_node_math.cpp

float TransitionNode::compute_progress(const RenderGraphContext& ctx) const {
    if (ctx.frame.frame.fps <= 0.0f) return 0.0f;

    const double fps = static_cast<double>(ctx.frame.frame.fps);
    // ctx.frame.frame.time_seconds = frame + frame_time (frame number, not seconds)
    // Convert to seconds
    double global_time_s = static_cast<double>(ctx.frame.frame.time_seconds) / fps;
    double layer_start_s = static_cast<double>(m_layer_from) / fps;
    double layer_time_s = global_time_s - layer_start_s;

    double duration_s = (m_layer_duration >= 0)
        ? (static_cast<double>(m_layer_duration) / fps)
        : std::numeric_limits<double>::infinity();

    double raw_progress = 0.0;

    if (!m_is_out) {
        // Transition in
        if (m_spec.duration > 0.0 && layer_time_s >= m_spec.delay) {
            raw_progress = (layer_time_s - m_spec.delay) / m_spec.duration;
        }
    } else if (std::isfinite(duration_s)) {
        // Transition out
        double out_start_s = duration_s - m_spec.duration - m_spec.delay;
        if (layer_time_s >= out_start_s) {
            if (layer_time_s < duration_s - m_spec.delay) {
                if (m_spec.duration > 0.0) {
                    raw_progress = (layer_time_s - out_start_s) / m_spec.duration;
                }
            } else {
                raw_progress = 1.0;
            }
        }
    }

    double clamped = std::clamp(raw_progress, 0.0, 1.0);
    return easing::apply(m_spec.easing, static_cast<float>(clamped));
}

OwnedFB TransitionNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>>
) {
    if (inputs.empty() || !inputs[0]) {
        return ctx.acquire_owned_fb(ctx.frame.frame.width, ctx.frame.frame.height, true);
    }

    const FramebufferRef& src = inputs[0];
    const i32 w = src->width();
    const i32 h = src->height();

    auto out_fb = ctx.acquire_owned_fb(w, h, false);

    if (m_spec.transition_id == "none" || m_spec.transition_id.empty()) {
        *out_fb = *src;
        return out_fb;
    }

    const float p = compute_progress(ctx);

    if (m_spec.transition_id == "crossfade") {
        const float t = m_is_out ? (1.0f - p) : p;
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                dst_row[x] = src_row[x] * t;
            }
        }
    } else if (m_spec.transition_id == "slide") {
        i32 dx_from = 0, dy_from = 0;
        i32 dx_to = 0, dy_to = 0;
        
        if (m_spec.direction == TransitionDirection::Left) {
            dx_from = -static_cast<i32>(std::round(w * p));
            dx_to = w - static_cast<i32>(std::round(w * p));
        } else if (m_spec.direction == TransitionDirection::Right) {
            dx_from = static_cast<i32>(std::round(w * p));
            dx_to = -w + static_cast<i32>(std::round(w * p));
        } else if (m_spec.direction == TransitionDirection::Up) {
            dy_from = -static_cast<i32>(std::round(h * p));
            dy_to = h - static_cast<i32>(std::round(h * p));
        } else if (m_spec.direction == TransitionDirection::Down) {
            dy_from = static_cast<i32>(std::round(h * p));
            dy_to = -h + static_cast<i32>(std::round(h * p));
        } else {
            dx_from = -static_cast<i32>(std::round(w * p));
            dx_to = w - static_cast<i32>(std::round(w * p));
        }

        for (i32 y = 0; y < h; ++y) {
            Color* dst_row = out_fb->pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                if (!m_is_out) {
                    i32 src_x = x - dx_to;
                    i32 src_y = y - dy_to;
                    if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h) {
                        dst_row[x] = src->get_pixel(src_x, src_y);
                    }
                } else {
                    i32 src_x = x - dx_from;
                    i32 src_y = y - dy_from;
                    if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h) {
                        dst_row[x] = src->get_pixel(src_x, src_y);
                    }
                }
            }
        }
    } else if (m_spec.transition_id == "wipe_linear") {
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);
                float val = 0.0f;
                if (m_spec.direction == TransitionDirection::Right) {
                    val = u;
                } else if (m_spec.direction == TransitionDirection::Left) {
                    val = 1.0f - u;
                } else if (m_spec.direction == TransitionDirection::Down) {
                    val = v;
                } else if (m_spec.direction == TransitionDirection::Up) {
                    val = 1.0f - v;
                } else {
                    val = u;
                }
                
                bool keep = false;
                if (!m_is_out) {
                    keep = (val <= p);
                } else {
                    keep = (val >= p);
                }
                dst_row[x] = keep ? src_row[x] : Color::transparent();
            }
        }
    } else if (m_spec.transition_id == "smooth_wipe") {
        const float w_feather = 0.1f;
        const float sweep_progress = p * (1.0f + w_feather);
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);
                float val = 0.0f;
                if (m_spec.direction == TransitionDirection::Right) {
                    val = u;
                } else if (m_spec.direction == TransitionDirection::Left) {
                    val = 1.0f - u;
                } else if (m_spec.direction == TransitionDirection::Down) {
                    val = v;
                } else if (m_spec.direction == TransitionDirection::Up) {
                    val = 1.0f - v;
                } else {
                    val = u;
                }
                
                float t = 0.0f;
                if (!m_is_out) {
                    t = std::clamp((sweep_progress - val) / w_feather, 0.0f, 1.0f);
                } else {
                    t = std::clamp((val - sweep_progress + w_feather) / w_feather, 0.0f, 1.0f);
                }
                t = t * t * (3.0f - 2.0f * t);
                dst_row[x] = src_row[x] * t;
            }
        }
    } else if (m_spec.transition_id == "circle_iris") {
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const float max_r = std::sqrt(cx * cx + cy * cy);
        const float r_feather = std::max(1.0f, max_r * 0.1f);
        const float sweep_r = p * (max_r + r_feather);

        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            const float dy = static_cast<float>(y) - cy;
            for (i32 x = 0; x < w; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dist = std::sqrt(dx * dx + dy * dy);
                float t = 0.0f;
                if (!m_is_out) {
                    t = std::clamp((sweep_r - dist) / r_feather, 0.0f, 1.0f);
                } else {
                    t = std::clamp((max_r - sweep_r - dist + r_feather) / r_feather, 0.0f, 1.0f);
                }
                t = t * t * (3.0f - 2.0f * t);
                dst_row[x] = src_row[x] * t;
            }
        }
    } else if (m_spec.transition_id == "flash") {
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                Color s = src_row[x];
                Color out_color = Color::transparent();
                if (!m_is_out) {
                    if (p < 0.5f) {
                        const float t = p / 0.5f;
                        out_color = Color::white() * t;
                        out_color.a = s.a * t;
                    } else {
                        const float t = (p - 0.5f) / 0.5f;
                        out_color = lerp(Color::white().with_alpha(s.a), s, t);
                    }
                } else {
                    if (p < 0.5f) {
                        const float t = p / 0.5f;
                        out_color = lerp(s, Color::white().with_alpha(s.a), t);
                    } else {
                        const float t = (p - 0.5f) / 0.5f;
                        out_color = Color::white() * (1.0f - t);
                        out_color.a = s.a * (1.0f - t);
                    }
                }
                dst_row[x] = out_color;
            }
        }
    } else if (m_spec.transition_id == "procedural_remotion") {
        const float seed = 1.2f;
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);
                
                float t_alpha = 0.0f;
                if (!m_is_out) {
                    t_alpha = transition_math::smoothstep01(std::clamp((p - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((p - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float mask = transition_math::procedural_remotion_mask(u, v, p, seed);
                float intensity = mask * 5.0f;
                
                Color inner_color = {1.0f, 0.45f, 0.0f, 1.0f};
                Color mid_color = {1.0f, 0.45f, 0.0f, 1.0f};
                Color outer_color = {1.0f, 1.0f, 1.0f, 1.0f};
                
                Color leak = lerp(inner_color, mid_color, mask);
                leak = lerp(leak, outer_color, mask * mask * mask);
                
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
    } else if (m_spec.transition_id == "remotion") {
        const float speed = 1.35f;
        const float direction = 3.0f;
        const float angle = 0.0f;
        for (i32 y = 0; y < h; ++y) {
            const Color* src_row = src->pixels_row(y);
            Color* dst_row = out_fb->pixels_row(y);
            const float v = static_cast<float>(y) / std::max(1, h);
            for (i32 x = 0; x < w; ++x) {
                const float u = static_cast<float>(x) / std::max(1, w);
                
                float t_alpha = 0.0f;
                if (!m_is_out) {
                    t_alpha = transition_math::smoothstep01(std::clamp((p - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((p - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float remotion_alpha = transition_math::remotion_mask(u, v, p, speed, direction);
                float remotion_r = 0.0f, remotion_g = 0.0f, remotion_b = 0.0f;
                transition_math::evaluate_remotion_color(u, v, p, speed, direction, angle, remotion_r, remotion_g, remotion_b);
                
                Color res;
                res.r = std::clamp(base.r * (1.0f - remotion_alpha) + remotion_r * remotion_alpha, 0.0f, 1.0f);
                res.g = std::clamp(base.g * (1.0f - remotion_alpha) + remotion_g * remotion_alpha, 0.0f, 1.0f);
                res.b = std::clamp(base.b * (1.0f - remotion_alpha) + remotion_b * remotion_alpha, 0.0f, 1.0f);
                res.a = std::clamp(base.a + remotion_alpha * 0.8f, 0.0f, 1.0f);
                
                dst_row[x] = res;
            }
        }
    } else {
        *out_fb = *src;
    }

    return out_fb;
}

} // namespace chronon3d::graph
