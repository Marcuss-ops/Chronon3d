#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <cmath>
#include <algorithm>

namespace chronon3d::graph {

namespace {

inline float noise_hash(float x, float y) {
    float val = std::sin(x * 12.9898f + y * 78.233f) * 43758.5453f;
    return val - std::floor(val);
}

inline float value_noise_2d(float x, float y) {
    float ix = std::floor(x);
    float iy = std::floor(y);
    float fx = x - ix;
    float fy = y - iy;

    float a = noise_hash(ix, iy);
    float b = noise_hash(ix + 1.0f, iy);
    float c = noise_hash(ix, iy + 1.0f);
    float d = noise_hash(ix + 1.0f, iy + 1.0f);

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);

    return (a * (1.0f - ux) + b * ux) * (1.0f - uy) + 
           (c * (1.0f - ux) + d * ux) * uy;
}

inline float smoothstep01(float val) {
    return val * val * (3.0f - 2.0f * val);
}

float procedural_remotion_mask(float u, float v, float t, float seed) {
    float p = (t < 0.5f) ? std::pow(t * 2.0f, 1.3f) : ((1.0f - t) * 2.0f);
    p = smoothstep01(p);

    float n1 = value_noise_2d(u * 2.5f + seed, v * 2.5f + t * 0.3f);
    float n2 = value_noise_2d(u * 4.0f - seed * 1.3f, v * 4.0f + t * 0.5f);
    float n3 = value_noise_2d(u * 1.7f + seed * 0.5f, v * 1.7f + t * 1.0f);

    float leak = std::pow(n1 * 0.6f + n2 * 0.3f + n3 * 0.1f, 2.0f);

    float dx = u - 0.5f;
    float dy = (v - 0.5f) * 0.5625f;
    float dist = std::sqrt(dx * dx + dy * dy);

    float edge_factor = std::clamp((dist - 0.05f) / 0.6f, 0.0f, 1.0f);
    float vignette = edge_factor * edge_factor * (3.0f - 2.0f * edge_factor);

    return leak * vignette * p * 2.0f;
}

static void remotion_pattern(float u, float v, float seed, float t,
                             float& out_brightness, float& out_blend, float& out_pattern) {
    float x = u * 0.8f;
    float y = v * 0.8f;
    x += std::sin(seed * 1.61803f) * 5.0f;
    y += std::cos(seed * 2.71828f) * 5.0f;

    for (int i = 1; i < 5; ++i) {
        float fi = static_cast<float>(i);
        float phase = seed * 0.7f * fi;
        float nx = x + (0.6f / fi) * std::cos(fi * y + t * 0.7f + 0.3f * fi + phase) + 20.0f;
        float ny = y + (0.6f / fi) * std::cos(fi * x + t * 0.7f + 0.3f * static_cast<float>(i + 10) + phase) - 20.0f + 15.0f;
        x = nx;
        y = ny;
    }

    float v1 = 0.5f * std::sin(2.0f * x) + 0.5f;
    float v2 = 0.5f * std::sin(2.0f * y) + 0.5f;
    out_blend = std::sin(x + y) * 0.5f + 0.5f;
    out_brightness = v1 * 0.5f + v2 * 0.5f;
    out_pattern = out_brightness * 0.6f + out_blend * 0.4f;
}

float remotion_mask(float u, float v, float t, float speed, float direction) {
    float aspect = 0.5625f;
    float refScale = 1.92f;
    float motion_scale = std::max(0.1f, speed);

    float speed_factor = 1.5f * motion_scale; 
    float evolve_progress = std::clamp(t * speed_factor, 0.0f, 1.0f);
    float evolve = std::pow(evolve_progress, 1.4f);
    evolve = smoothstep01(evolve);

    float retract = smoothstep01(std::clamp(t * speed_factor - 1.2f, 0.0f, 1.0f));

    float su = u * refScale;
    float sv = v * refScale * aspect;

    float b1, bl1, pv1;
    remotion_pattern(su, sv, direction, evolve * 3.14159265f * motion_scale, b1, bl1, pv1);

    float threshA = 1.0f - evolve;
    float revealAlpha = std::clamp((pv1 - threshA) / 0.3f, 0.0f, 1.0f);
    revealAlpha = revealAlpha * revealAlpha * (3.0f - 2.0f * revealAlpha);

    float maxU = refScale;
    float maxV = refScale * aspect;
    float ru = maxU - su;
    float rv = maxV - sv;

    float b2, bl2, pv2;
    remotion_pattern(ru, rv, direction + 42.0f, retract * 3.14159265f * motion_scale, b2, bl2, pv2);

    float threshB = 1.0f - retract;
    float eraseAlpha = std::clamp((pv2 - threshB) / 0.3f, 0.0f, 1.0f);
    eraseAlpha = eraseAlpha * eraseAlpha * (3.0f - 2.0f * eraseAlpha);

    float mask = std::clamp(revealAlpha * (1.0f - eraseAlpha), 0.0f, 1.0f);
    
    float global_fade = std::pow(std::clamp(t * 2.0f, 0.0f, 1.0f), 1.2f); 
    return mask * global_fade;
}

void evaluate_remotion_color(float u, float v, float t, float speed, float direction, float angle,
                             float& out_r, float& out_g, float& out_b) {
    float aspect = 0.5625f;
    float refScale = 1.92f;
    float motion_scale = std::max(0.1f, speed);
    float evolve = std::min(1.0f, t * 2.4f * motion_scale);

    float su = u * refScale;
    float sv = v * refScale * aspect;

    float brightness, blend, pattern;
    remotion_pattern(su, sv, direction, evolve * 3.14159265f * motion_scale, brightness, blend, pattern);

    float base_r = 1.0f;
    float base_g = 0.93f + (0.62f - 0.93f) * blend;
    float base_b = 0.12f + (0.03f - 0.12f) * blend;

    float brightness_factor = 0.65f + 0.55f * brightness;
    base_r *= brightness_factor;
    base_g *= brightness_factor;
    base_b *= brightness_factor;

    float angle_rad = angle * 3.14159265f / 180.0f;
    float cosA = std::cos(angle_rad);
    float sinA = std::sin(angle_rad);
    float inv3 = 1.0f / 3.0f;
    float sqrt3 = 0.57735f;

    float m00 = cosA + (1.0f - cosA) * inv3;
    float m01 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m02 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m10 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m11 = cosA + (1.0f - cosA) * inv3;
    float m12 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m20 = (1.0f - cosA) * inv3 - sinA * sqrt3;
    float m21 = (1.0f - cosA) * inv3 + sinA * sqrt3;
    float m22 = cosA + (1.0f - cosA) * inv3;

    out_r = std::clamp(m00 * base_r + m01 * base_g + m02 * base_b, 0.0f, 1.0f);
    out_g = std::clamp(m10 * base_r + m11 * base_g + m12 * base_b, 0.0f, 1.0f);
    out_b = std::clamp(m20 * base_r + m21 * base_g + m22 * base_b, 0.0f, 1.0f);
}

} // namespace


std::shared_ptr<Framebuffer> TransitionNode::execute(
    RenderGraphContext& ctx,
    const std::vector<std::shared_ptr<Framebuffer>>& inputs,
    const std::vector<std::optional<raster::BBox>>&
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
                    t_alpha = smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float mask = procedural_remotion_mask(u, v, m_progress, seed);
                float intensity = mask * 5.0f;
                
                Color inner_color = {1.0f, 0.45f, 0.0f, 1.0f};
                Color mid_color = {1.0f, 0.45f, 0.0f, 1.0f};
                Color outer_color = {1.0f, 1.0f, 1.0f, 1.0f};
                
                Color leak = lerp(inner_color, mid_color, mask);
                leak = lerp(leak, outer_color, mask * mask * mask);
                
                float hole_factor = smoothstep01(std::clamp(mask / 0.4f, 0.0f, 1.0f));
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
                    t_alpha = smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                } else {
                    t_alpha = 1.0f - smoothstep01(std::clamp((m_progress - 0.2f) / 0.6f, 0.0f, 1.0f));
                }
                
                Color base = src_row[x] * t_alpha;
                
                float remotion_alpha = remotion_mask(u, v, m_progress, speed, direction);
                float remotion_r = 0.0f, remotion_g = 0.0f, remotion_b = 0.0f;
                evaluate_remotion_color(u, v, m_progress, speed, direction, angle, remotion_r, remotion_g, remotion_b);
                
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
