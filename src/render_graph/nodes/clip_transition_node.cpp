#include <chronon3d/render_graph/nodes/clip_transition_node.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/transition/transition_progress_sampler.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include <algorithm>
#include <cmath>
#include <span>
#include <stdexcept>

namespace chronon3d::graph {

namespace {

float scale_coord(float out_coord, int out_size, int src_size) {
    if (src_size <= 1 || out_size <= 1) return 0.5f;
    const float norm = (out_coord + 0.5f) / static_cast<float>(out_size);
    return norm * (src_size - 1) + 0.5f;
}

Color sample_at(const Framebuffer& src, int x, int y, int out_w, int out_h) {
    const float sx = scale_coord(static_cast<float>(x), out_w, src.width());
    const float sy = scale_coord(static_cast<float>(y), out_h, src.height());
    return src.sample_bilinear(sx, sy);
}

void sample_into(const Framebuffer& src, Framebuffer& dst, int out_w, int out_h) {
    for (int y = 0; y < out_h; ++y) {
        Color* row_out = dst.pixels_row(y);
        for (int x = 0; x < out_w; ++x) {
            row_out[x] = sample_at(src, x, y, out_w, out_h);
        }
    }
}

Color sample_uv(const Framebuffer& src, float u, float v) {
    const float sx = u * (src.width() - 1);
    const float sy = v * (src.height() - 1);
    return src.sample_bilinear(sx, sy);
}

float compute_clip_progress(const ClipTransitionSpec& spec, Frame from, Frame duration,
                             const RenderGraphContext& ctx) {
    if (ctx.frame_input.fps <= 0.0f) {
        return 0.0f;
    }

    const float fps = ctx.frame_input.fps;
    const FrameRate rate{static_cast<i32>(std::round(fps * 1000.0f)), 1000};
    const double frame = static_cast<double>(ctx.frame_input.frame);

    const Frame start = from;
    const Frame end = start + (duration > 0 ? duration : Frame{1});

    const auto sample = sample_transition(
        SampleTime::from_frame(frame, rate),
        SampleTime::from_frame_int(start, rate),
        SampleTime::from_frame_int(end, rate),
        EasingCurve{spec.easing},
        TransitionProgressDirection::In);

    return sample.eased_progress;
}

Vec2 direction_vector(TransitionDirection dir) {
    switch (dir) {
        case TransitionDirection::Left:  return Vec2{-1.0f, 0.0f};
        case TransitionDirection::Right: return Vec2{ 1.0f, 0.0f};
        case TransitionDirection::Up:    return Vec2{ 0.0f,-1.0f};
        case TransitionDirection::Down:  return Vec2{ 0.0f, 1.0f};
        default:                       return Vec2{ 1.0f, 0.0f};
    }
}

float select_directional_mask(TransitionDirection dir, float u, float v) {
    switch (dir) {
        case TransitionDirection::Left:  return 1.0f - u;
        case TransitionDirection::Right: return u;
        case TransitionDirection::Up:    return 1.0f - v;
        case TransitionDirection::Down:  return v;
        default:                         return u;
    }
}

float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

ClipTransitionNode::ClipTransitionNode(std::string name, ClipTransitionSpec spec, Frame from, Frame duration)
    : RenderGraphNode(frame_variant_cache("clip_transition"))
    , m_name(std::move(name))
    , m_spec(std::move(spec))
    , m_from(from)
    , m_duration(duration) {
}

cache::NodeCacheKey ClipTransitionNode::cache_key(const RenderGraphContext& ctx) const {
    cache::NodeCacheKey key{
        .scope = "clip_transition:" + m_name,
        .frame = ctx.frame_input.frame,
        .width = ctx.frame_input.width,
        .height = ctx.frame_input.height,
    };

    u64 params_hash = 0;
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.kind));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.easing));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.fit));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.direction));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_from.value));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_duration.value));
    params_hash = hash_combine(params_hash, static_cast<u64>(std::hash<float>{}(ctx.frame_input.fps)));
    params_hash = hash_combine(params_hash, static_cast<u64>(std::hash<float>{}(m_spec.center.x)));
    params_hash = hash_combine(params_hash, static_cast<u64>(std::hash<float>{}(m_spec.center.y)));
    params_hash = hash_combine(params_hash, static_cast<u64>(std::hash<float>{}(m_spec.feather)));
    params_hash = hash_combine(params_hash, static_cast<u64>(std::hash<float>{}(m_spec.zoom_scale)));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.flash_color.r * 255.0f));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.flash_color.g * 255.0f));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.flash_color.b * 255.0f));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_spec.flash_color.a * 255.0f));
    key.params_hash = params_hash;

    return key;
}

std::optional<raster::BBox> ClipTransitionNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>>
) const {
    // A clip transition always produces a full-canvas framebuffer.
    return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
}

NodeExecResult ClipTransitionNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>>
) {
    if (inputs.size() < 2) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "ClipTransitionNode requires exactly two inputs (A and B)"
        }};
    }

    const FramebufferRef& a = inputs[0];
    const FramebufferRef& b = inputs[1];

    if (!a || !b) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "ClipTransitionNode received null input framebuffer"
        }};
    }

    const int out_w = ctx.frame_input.width;
    const int out_h = ctx.frame_input.height;
    if (out_w <= 0 || out_h <= 0) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "ClipTransitionNode output dimensions must be positive"
        }};
    }

    if (m_spec.fit == ClipTransitionFitPolicy::ErrorOnMismatch &&
        (a->width() != b->width() || a->height() != b->height())) {
        return NodeExecResult{NodeExecutionError{
            RenderBackendErrorCode::InvalidInput,
            m_name,
            "ClipTransitionNode inputs A and B must have the same dimensions"
        }};
    }

    const float p = compute_clip_progress(m_spec, m_from, m_duration, ctx);

    // Fast path: Cut with matching dimensions is just a copy of the
    // selected input.  The output canvas matches, so we can swap.
    if (m_spec.kind == ClipTransitionKind::Cut &&
        a->width() == out_w && a->height() == out_h &&
        b->width() == out_w && b->height() == out_h) {
        const FramebufferRef& src = (p < 1.0f) ? a : b;
        auto out = ctx.acquire_owned_fb(*src);
        return NodeExecResult{std::move(out)};
    }

    // General path: sample inputs into the output canvas using bilinear
    // filtering.  This handles mismatched resolutions and clips larger
    // inputs to the output bounds.
    auto out = ctx.acquire_owned_fb(out_w, out_h, false);
    out->set_opaque(false);

    if (m_spec.kind == ClipTransitionKind::Cut) {
        const FramebufferRef& src = (p < 1.0f) ? a : b;
        sample_into(*src, *out, out_w, out_h);
        return NodeExecResult{std::move(out)};
    }

    if (m_spec.kind == ClipTransitionKind::Dissolve) {
        const float one_minus_p = 1.0f - p;
        for (int y = 0; y < out_h; ++y) {
            Color* row_out = out->pixels_row(y);
            for (int x = 0; x < out_w; ++x) {
                const Color ca = sample_at(*a, x, y, out_w, out_h);
                const Color cb = sample_at(*b, x, y, out_w, out_h);
                Color result;
                result.r = ca.r * one_minus_p + cb.r * p;
                result.g = ca.g * one_minus_p + cb.g * p;
                result.b = ca.b * one_minus_p + cb.b * p;
                result.a = ca.a * one_minus_p + cb.a * p;
                row_out[x] = result;
            }
        }
        return NodeExecResult{std::move(out)};
    }

    const float inv_w = 1.0f / std::max(1, out_w);
    const float inv_h = 1.0f / std::max(1, out_h);

    switch (m_spec.kind) {
        case ClipTransitionKind::Push: {
            const Vec2 dir = direction_vector(m_spec.direction);
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    // A is pushed out in the direction; B follows from the opposite side.
                    const float ua = u + p * dir.x;
                    const float va = v + p * dir.y;
                    const float ub = u - (1.0f - p) * dir.x;
                    const float vb = v - (1.0f - p) * dir.y;
                    const Color ca = (ua >= 0.0f && ua <= 1.0f && va >= 0.0f && va <= 1.0f)
                        ? sample_uv(*a, ua, va) : Color::transparent();
                    const Color cb = (ub >= 0.0f && ub <= 1.0f && vb >= 0.0f && vb <= 1.0f)
                        ? sample_uv(*b, ub, vb) : Color::transparent();
                    // Composite B over A (inputs are premultiplied).
                    const float inv_a = 1.0f - cb.a;
                    row_out[x] = Color{
                        ca.r * inv_a + cb.r,
                        ca.g * inv_a + cb.g,
                        ca.b * inv_a + cb.b,
                        ca.a * inv_a + cb.a
                    };
                }
            }
            return NodeExecResult{std::move(out)};
        }

        case ClipTransitionKind::Slide: {
            const Vec2 dir = direction_vector(m_spec.direction);
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    const Color ca = sample_uv(*a, u, v);
                    // B slides in from the direction side (e.g. Right means B enters from the right).
                    const float ub = u - (1.0f - p) * dir.x;
                    const float vb = v - (1.0f - p) * dir.y;
                    const Color cb = (ub >= 0.0f && ub <= 1.0f && vb >= 0.0f && vb <= 1.0f)
                        ? sample_uv(*b, ub, vb) : Color::transparent();
                    // Composite B over A (inputs are premultiplied).
                    const float inv_a = 1.0f - cb.a;
                    row_out[x] = Color{
                        ca.r * inv_a + cb.r,
                        ca.g * inv_a + cb.g,
                        ca.b * inv_a + cb.b,
                        ca.a * inv_a + cb.a
                    };
                }
            }
            return NodeExecResult{std::move(out)};
        }

        case ClipTransitionKind::Wipe: {
            const float feather = std::max(0.0001f, m_spec.feather);
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    const float mask_u = select_directional_mask(m_spec.direction, u, v);
                    const float sweep = p * (1.0f + feather);
                    const float mask = smoothstep01(std::clamp((sweep - mask_u) / feather, 0.0f, 1.0f));
                    const Color ca = sample_uv(*a, u, v);
                    const Color cb = sample_uv(*b, u, v);
                    Color result;
                    result.r = ca.r * (1.0f - mask) + cb.r * mask;
                    result.g = ca.g * (1.0f - mask) + cb.g * mask;
                    result.b = ca.b * (1.0f - mask) + cb.b * mask;
                    result.a = ca.a * (1.0f - mask) + cb.a * mask;
                    row_out[x] = result;
                }
            }
            return NodeExecResult{std::move(out)};
        }

        case ClipTransitionKind::Iris: {
            const float aspect = static_cast<float>(out_w) / static_cast<float>(std::max(1, out_h));
            const float feather = std::max(0.0001f, m_spec.feather);
            const float cx = m_spec.center.x;
            const float cy = m_spec.center.y;
            // Compute the radius in aspect-corrected space so the iris stays
            // circular on non-square canvases.
            const float max_r_x = std::max(cx * aspect, (1.0f - cx) * aspect);
            const float max_r_y = std::max(cy, 1.0f - cy);
            const float max_r = std::sqrt(max_r_x * max_r_x + max_r_y * max_r_y);
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    const float dx = (u - cx) * aspect;
                    const float dy = v - cy;
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    const float r_feather = std::max(0.001f, max_r * feather);
                    const float sweep_r = p * (max_r + r_feather);
                    const float mask = smoothstep01(std::clamp((sweep_r - dist) / r_feather, 0.0f, 1.0f));
                    const Color ca = sample_uv(*a, u, v);
                    const Color cb = sample_uv(*b, u, v);
                    Color result;
                    result.r = ca.r * (1.0f - mask) + cb.r * mask;
                    result.g = ca.g * (1.0f - mask) + cb.g * mask;
                    result.b = ca.b * (1.0f - mask) + cb.b * mask;
                    result.a = ca.a * (1.0f - mask) + cb.a * mask;
                    row_out[x] = result;
                }
            }
            return NodeExecResult{std::move(out)};
        }

        case ClipTransitionKind::Zoom: {
            const float cx = m_spec.center.x;
            const float cy = m_spec.center.y;
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    const float scale = 1.0f + p * m_spec.zoom_scale;
                    const float ub = (u - cx) / scale + cx;
                    const float vb = (v - cy) / scale + cy;
                    const Color ca = sample_uv(*a, u, v) * (1.0f - p);
                    const Color cb = (ub >= 0.0f && ub <= 1.0f && vb >= 0.0f && vb <= 1.0f)
                        ? sample_uv(*b, ub, vb) * p : Color::transparent();
                    row_out[x] = ca + cb;
                }
            }
            return NodeExecResult{std::move(out)};
        }

        case ClipTransitionKind::Flash: {
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const float u = (x + 0.5f) * inv_w;
                    const float v = (y + 0.5f) * inv_h;
                    const Color ca = sample_uv(*a, u, v);
                    const Color cb = sample_uv(*b, u, v);
                    const float flash_alpha = 1.0f - std::abs(p - 0.5f) * 2.0f;
                    const Color flash = m_spec.flash_color.with_alpha(flash_alpha);
                    Color result;
                    if (p < 0.5f) {
                        const float t = p / 0.5f;
                        result = lerp(ca, flash, t);
                    } else {
                        const float t = (p - 0.5f) / 0.5f;
                        result = lerp(flash, cb, t);
                    }
                    row_out[x] = result;
                }
            }
            return NodeExecResult{std::move(out)};
        }

        default: {
            // Defensive fallback to Dissolve for any unhandled kind.
            const float one_minus_p = 1.0f - p;
            for (int y = 0; y < out_h; ++y) {
                Color* row_out = out->pixels_row(y);
                for (int x = 0; x < out_w; ++x) {
                    const Color ca = sample_at(*a, x, y, out_w, out_h);
                    const Color cb = sample_at(*b, x, y, out_w, out_h);
                    Color result;
                    result.r = ca.r * one_minus_p + cb.r * p;
                    result.g = ca.g * one_minus_p + cb.g * p;
                    result.b = ca.b * one_minus_p + cb.b * p;
                    result.a = ca.a * one_minus_p + cb.a * p;
                    row_out[x] = result;
                }
            }
            return NodeExecResult{std::move(out)};
        }
    }
}

} // namespace chronon3d::graph
