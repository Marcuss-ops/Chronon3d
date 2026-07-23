#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/animation/easing/easing.hpp>
#include <chronon3d/animation/transition/transition_progress_sampler.hpp>
#include <chronon3d/render_graph/render_graph_context.hpp>
#include <cmath>
#include <algorithm>
#include <span>
#include <limits>

namespace chronon3d::graph {

float TransitionNode::compute_progress(const RenderGraphContext& ctx) const {
    if (ctx.frame_input.fps <= 0.0f) {
        return 0.0f;
    }

    const float fps = ctx.frame_input.fps;
    const FrameRate rate{static_cast<i32>(std::round(fps * 1000.0f)), 1000};
    const double frame = static_cast<double>(ctx.frame_input.frame);
    const Frame layer_start = m_layer_from;
    const Frame layer_end = (m_layer_duration >= 0) ? (m_layer_from + m_layer_duration) : Frame{std::numeric_limits<i64>::max()};

    const Frame delay_frames{static_cast<i64>(std::llround(m_spec.delay * fps))};
    const Frame duration_frames{static_cast<i64>(std::llround(m_spec.duration * fps))};

    SampleTime current = SampleTime::from_frame(frame, rate);
    SampleTime start;
    SampleTime end;

    if (!m_is_out) {
        start = SampleTime::from_frame_int(layer_start + delay_frames, rate);
        end = SampleTime::from_frame_int(layer_start + delay_frames + duration_frames, rate);
    } else {
        // The out transition ends at the layer end minus delay.
        start = SampleTime::from_frame_int(layer_end - delay_frames - duration_frames, rate);
        end = SampleTime::from_frame_int(layer_end - delay_frames, rate);
    }

    // Clamp the transition window to the layer lifetime so a long duration
    // or delay does not pull the transition outside the layer.
    const Frame clamped_start{std::max(static_cast<i64>(layer_start), static_cast<i64>(std::llround(start.frame)))};
    Frame clamped_end{std::min(layer_end.value, static_cast<i64>(std::llround(end.frame)))};
    // Degenerate transitions collapse to a single-frame cut.
    if (clamped_start >= clamped_end) {
        clamped_end = clamped_start + 1;
    }

    // Both in and out transitions progress from 0 (start) to 1 (end).
    // The program itself decides how to interpret is_out (e.g. crossfade out
    // uses 1 - progress).  The sampler only needs the forward direction.
    const EasingCurve easing_curve{m_spec.easing};
    const auto sample = sample_transition(
        current,
        SampleTime::from_frame_int(clamped_start, rate),
        SampleTime::from_frame_int(clamped_end, rate),
        easing_curve,
        TransitionProgressDirection::In);
    return sample.eased_progress;
}

cache::NodeCacheKey TransitionNode::cache_key(const RenderGraphContext& ctx) const {
    cache::NodeCacheKey key{
        .scope = "transition:" + m_layer_name,
        .frame = ctx.frame_input.frame,
        .width = ctx.frame_input.width,
        .height = ctx.frame_input.height,
    };
    key.params_hash = hash_layer_transition_spec(m_spec);
    key.params_hash = hash_combine(key.params_hash, static_cast<u64>(m_is_out));
    return key;
}

std::optional<raster::BBox> TransitionNode::predicted_bbox(
    const RenderGraphContext& ctx,
    std::span<const std::optional<raster::BBox>> input_bboxes
) const {
    std::optional<raster::BBox> input_bbox;
    if (!input_bboxes.empty()) {
        input_bbox = input_bboxes[0];
    }

    // Empty id means "no transition": keep the input bbox unchanged and
    // avoid touching the catalog.  If there is no input, fall back to the
    // full output canvas.
    if (m_spec.transition_id.empty()) {
        return input_bbox.value_or(raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height});
    }

    // Delegates to the program so the node stays free of string-based
    // transition dispatch.  Empty / missing input falls back to the
    // full output canvas.
    if (!input_bbox.has_value()) {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    const LayerTransitionProgram& program = resolve_program(ctx);
    return program.predicted_bbox(input_bbox, ctx);
}

const LayerTransitionProgram& TransitionNode::resolve_program(const RenderGraphContext& ctx) const {
    if (m_program) {
        return *m_program;
    }

    if (!ctx.services.transition_catalog) {
        throw std::runtime_error("TransitionNode: no LayerTransitionCatalog wired in RenderServices");
    }

    m_program = ctx.services.transition_catalog->resolve(m_spec);
    if (!m_program) {
        throw std::runtime_error("TransitionNode: catalog returned null program for " + m_spec.transition_id);
    }
    return *m_program;
}

NodeExecResult TransitionNode::execute(
    RenderGraphContext& ctx,
    std::span<const FramebufferRef> inputs,
    std::span<const std::optional<raster::BBox>>
) {
    if (inputs.empty() || !inputs[0]) {
        return NodeExecResult{ctx.acquire_owned_fb(ctx.frame_input.width, ctx.frame_input.height, true)};
    }

    const FramebufferRef& src = inputs[0];
    const i32 w = src->width();
    const i32 h = src->height();

    auto out_fb = ctx.acquire_owned_fb(w, h, false);

    if (m_spec.transition_id.empty()) {
        *out_fb = *src;
        return NodeExecResult{std::move(out_fb)};
    }

    const float p = compute_progress(ctx);
    const LayerTransitionProgram& program = resolve_program(ctx);
    program.execute(src, *out_fb, p, m_spec.direction, m_is_out, ctx);

    return NodeExecResult{std::move(out_fb)};
}

} // namespace chronon3d::graph
