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
    params_hash = hash_combine(params_hash, static_cast<u64>(m_from.value));
    params_hash = hash_combine(params_hash, static_cast<u64>(m_duration.value));
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

    // Dissolve: output = A*(1-p) + B*p in premultiplied alpha space.
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

} // namespace chronon3d::graph
