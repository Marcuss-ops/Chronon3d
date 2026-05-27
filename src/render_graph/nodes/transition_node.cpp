#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <cmath>
#include <algorithm>
#include <span>

namespace chronon3d::graph {

// Math helpers live in transition/transition_node_math.cpp


std::shared_ptr<Framebuffer> TransitionNode::execute(
    RenderGraphContext& ctx,
    std::span<const std::shared_ptr<Framebuffer>> inputs,
    std::span<const std::optional<raster::BBox>>
) {
    if (inputs.empty() || !inputs[0]) {
        return ctx.acquire_framebuffer(ctx.width, ctx.height, true);
    }

    const auto& src = inputs[0];
    const i32 w = src->width();
    const i32 h = src->height();

    auto out_fb = ctx.acquire_framebuffer(w, h, false);

    if (m_spec.transition_id == "none" || m_spec.transition_id.empty()) {
        *out_fb = *src;
        return out_fb;
    }

    if (m_spec.transition_id == "crossfade") {
        const float t = m_is_out ? (1.0f - m_progress) : m_progress;
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
            dx_from = -static_cast<i32>(std::round(w * m_progress));
            dx_to = w - static_cast<i32>(std::round(w * m_progress));
        } else if (m_spec.direction == TransitionDirection::Right) {
            dx_from = static_cast<i32>(std::round(w * m_progress));
            dx_to = -w + static_cast<i32>(std::round(w * m_progress));
        } else if (m_spec.direction == TransitionDirection::Up) {
            dy_from = -static_cast<i32>(std::round(h * m_progress));
            dy_to = h - static_cast<i32>(std::round(h * m_progress));
        } else if (m_spec.direction == TransitionDirection::Down) {
            dy_from = static_cast<i32>(std::round(h * m_progress));
            dy_to = -h + static_cast<i32>(std::round(h * m_progress));
        } else {
            // Default to sliding left
            dx_from = -static_cast<i32>(std::round(w * m_progress));
            dx_to = w - static_cast<i32>(std::round(w * m_progress));
        }

        for (i32 y = 0; y < h; ++y) {
            Color* dst_row = out_fb->pixels_row(y);
            for (i32 x = 0; x < w; ++x) {
                if (!m_is_out) {
                    // transition_in: from is transparent, to is src
                    i32 src_x = x - dx_to;
                    i32 src_y = y - dy_to;
                    if (src_x >= 0 && src_x < w && src_y >= 0 && src_y < h) {
                        dst_row[x] = src->get_pixel(src_x, src_y);
                    }
                } else {
                    // transition_out: from is src, to is transparent
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
                    keep = (val <= m_progress);
                } else {
                    keep = (val >= m_progress);
                }
                dst_row[x] = keep ? src_row[x] : Color::transparent();
            }
        }
    } else if (m_spec.transition_id == "smooth_wipe") {
        const float w_feather = 0.1f;
        const float sweep_progress = m_progress * (1.0f + w_feather);
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
        const float sweep_r = m_progress * (max_r + r_feather);

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
                    if (m_progress < 0.5f) {
                        const float t = m_progress / 0.5f;
                        out_color = Color::white() * t;
                        out_color.a = s.a * t;
                    } else {
                        const float t = (m_progress - 0.5f) / 0.5f;
                        out_color = lerp(Color::white().with_alpha(s.a), s, t);
                    }
                } else {
                    if (m_progress < 0.5f) {
                        const float t = m_progress / 0.5f;
                        out_color = lerp(s, Color::white().with_alpha(s.a), t);
                    } else {
                        const float t = (m_progress - 0.5f) / 0.5f;
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
                    t_alpha = transition_math::smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float mask = transition_math::procedural_remotion_mask(u, v, m_progress, seed);
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
                    t_alpha = transition_math::smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - transition_math::smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float remotion_alpha = transition_math::remotion_mask(u, v, m_progress, speed, direction);
                float remotion_r = 0.0f, remotion_g = 0.0f, remotion_b = 0.0f;
                transition_math::evaluate_remotion_color(u, v, m_progress, speed, direction, angle, remotion_r, remotion_g, remotion_b);
                
                Color res;
                res.r = std::clamp(base.r * (1.0f - remotion_alpha) + remotion_r * remotion_alpha, 0.0f, 1.0f);
                res.g = std::clamp(base.g * (1.0f - remotion_alpha) + remotion_g * remotion_alpha, 0.0f, 1.0f);
                res.b = std::clamp(base.b * (1.0f - remotion_alpha) + remotion_b * remotion_alpha, 0.0f, 1.0f);
                res.a = std::clamp(base.a + remotion_alpha * 0.8f, 0.0f, 1.0f);
                
                dst_row[x] = res;
            }
        }
    } else {
        // Fallback: copy source
        *out_fb = *src;
    }

    return out_fb;
}

} // namespace chronon3d::graph
