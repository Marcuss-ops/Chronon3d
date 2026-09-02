#pragma once

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/render_graph/compiler/compiled_frame_graph.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace chronon3d::graph {

/// Hermetic execution contract for a useful frame segment.
///
/// The compiler owns temporal-halo derivation from the already-compiled graph.
/// Media/demux code may provide a GOP/keyframe seek anchor, but the compiler
/// validates that the anchor is early enough to satisfy the declared preroll.
/// RenderingGen remains responsible for orchestration, chunking, retries and
/// assignment of these descriptors to workers.
struct SegmentExecutionDescriptor {
    TimeRange range{};              ///< useful output range [start, end)
    Frame seek_anchor{0};           ///< decode start, normally a GOP/keyframe anchor
    Frame preroll{0};               ///< required history before range.start
    Frame postroll{0};              ///< required future after range.end
    std::uint64_t plan_hash{0};
    std::uint64_t asset_manifest_hash{0};
    std::uint64_t seed{0};

    [[nodiscard]] TimeRange required_range() const {
        const i64 start = range.start.integral();
        const i64 pre = preroll.integral();
        const i64 end = range.end.integral();
        const i64 post = postroll.integral();
        if (pre < 0 || post < 0) {
            throw std::logic_error("segment temporal halo must be non-negative");
        }
        const i64 required_start = pre > start ? 0 : start - pre;
        if (post > 0 && end > std::numeric_limits<i64>::max() - post) {
            throw std::overflow_error("segment postroll overflows frame range");
        }
        return TimeRange{Frame{required_start}, Frame{end + post}};
    }

    [[nodiscard]] TimeRange decode_range() const {
        const auto required = required_range();
        if (seek_anchor < Frame{0} || seek_anchor > required.start) {
            throw std::logic_error("segment seek anchor does not cover required preroll");
        }
        return TimeRange{seek_anchor, required.end};
    }

    [[nodiscard]] bool valid() const noexcept {
        if (range.start < Frame{0} || range.end < range.start ||
            seek_anchor < Frame{0} || preroll < Frame{0} || postroll < Frame{0}) {
            return false;
        }
        const i64 start = range.start.integral();
        const i64 pre = preroll.integral();
        const Frame required_start{pre > start ? 0 : start - pre};
        return seek_anchor <= required_start;
    }
};

namespace detail {

[[nodiscard]] inline bool rational_time_less(RationalTime lhs, RationalTime rhs) {
    const Rational a = normalize_rational(lhs.time_base);
    const Rational b = normalize_rational(rhs.time_base);
    if (a.numerator < 0 || b.numerator < 0) {
        throw std::invalid_argument("duration time-base must be non-negative");
    }
    const __int128 left = static_cast<__int128>(lhs.value) * a.numerator * b.denominator;
    const __int128 right = static_cast<__int128>(rhs.value) * b.numerator * a.denominator;
    return left < right;
}

[[nodiscard]] inline RationalTime max_duration(RationalTime lhs, RationalTime rhs) {
    if (lhs.value < 0 || rhs.value < 0) {
        throw std::invalid_argument("temporal duration must be non-negative");
    }
    return rational_time_less(lhs, rhs) ? rhs : lhs;
}

[[nodiscard]] inline Frame duration_to_frames_ceil(RationalTime duration, FrameRate rate) {
    if (duration.value < 0) {
        throw std::invalid_argument("temporal duration must be non-negative");
    }
    if (rate.numerator <= 0 || rate.denominator <= 0) {
        throw std::invalid_argument("segment frame rate must be positive");
    }
    const Rational base = normalize_rational(duration.time_base);
    if (base.numerator < 0) {
        throw std::invalid_argument("duration time-base must be non-negative");
    }
    if (duration.value == 0 || base.numerator == 0) {
        return Frame{0};
    }

    const __int128 numerator = static_cast<__int128>(duration.value) *
                               static_cast<__int128>(base.numerator) *
                               static_cast<__int128>(rate.numerator);
    const __int128 denominator = static_cast<__int128>(base.denominator) *
                                 static_cast<__int128>(rate.denominator);
    const __int128 frames = (numerator + denominator - 1) / denominator;
    if (frames > static_cast<__int128>(std::numeric_limits<i64>::max())) {
        throw std::overflow_error("temporal halo exceeds frame range");
    }
    return Frame{static_cast<i64>(frames)};
}

} // namespace detail

/// Aggregate the temporal dependency contract of reachable compiled nodes.
/// This is deliberately derived from CompiledFrameGraph rather than from the
/// authored graph so eliminated/dead nodes cannot enlarge distributed halos.
[[nodiscard]] inline TemporalRequirements aggregate_temporal_requirements(
    const CompiledFrameGraph& compiled) {
    TemporalRequirements aggregate{};
    const std::size_t count = std::min(compiled.nodes.size(), compiled.graph.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (!compiled.nodes[i].reachable || !compiled.graph.has_node(static_cast<GraphNodeId>(i))) {
            continue;
        }
        const auto requirement = compiled.graph.node(static_cast<GraphNodeId>(i)).temporal_requirements();
        if (!requirement.valid() || requirement.history_duration.value < 0 ||
            requirement.future_duration.value < 0) {
            throw std::invalid_argument("compiled node exposes invalid temporal requirements");
        }
        aggregate.history_frames = std::max(aggregate.history_frames, requirement.history_frames);
        aggregate.future_frames = std::max(aggregate.future_frames, requirement.future_frames);
        aggregate.history_duration = detail::max_duration(
            aggregate.history_duration, requirement.history_duration);
        aggregate.future_duration = detail::max_duration(
            aggregate.future_duration, requirement.future_duration);
    }
    return aggregate;
}

/// Compile one hermetic segment descriptor from the canonical compiled graph.
/// `frame_rate` is mandatory so duration-based requirements never fall back to
/// an implicit 30 fps/nominal source clock. `media_seek_anchor` is optional:
/// when present it should be the previous GOP/keyframe chosen by the media
/// layer; when absent the exact required-range start is used.
[[nodiscard]] inline SegmentExecutionDescriptor compile_segment_execution_descriptor(
    const CompiledFrameGraph& compiled,
    TimeRange range,
    FrameRate frame_rate,
    std::optional<Frame> media_seek_anchor = std::nullopt,
    std::uint64_t asset_manifest_hash = 0,
    std::uint64_t seed = 0) {
    if (!compiled.valid) {
        throw std::invalid_argument("cannot compile segment from invalid frame graph");
    }
    if (range.start < Frame{0} || range.end < range.start) {
        throw std::invalid_argument("segment range must be a non-negative half-open interval");
    }

    const auto temporal = aggregate_temporal_requirements(compiled);
    const Frame preroll = std::max(
        Frame{temporal.history_frames},
        detail::duration_to_frames_ceil(temporal.history_duration, frame_rate));
    const Frame postroll = std::max(
        Frame{temporal.future_frames},
        detail::duration_to_frames_ceil(temporal.future_duration, frame_rate));

    const i64 start = range.start.integral();
    const i64 pre = preroll.integral();
    const Frame required_start{pre > start ? 0 : start - pre};
    Frame seek_anchor = media_seek_anchor.value_or(required_start);
    if (seek_anchor < Frame{0}) {
        seek_anchor = Frame{0};
    }
    if (seek_anchor > required_start) {
        throw std::invalid_argument("media seek anchor is after the required preroll start");
    }

    SegmentExecutionDescriptor descriptor{
        .range = range,
        .seek_anchor = seek_anchor,
        .preroll = preroll,
        .postroll = postroll,
        .plan_hash = compiled.structure_hash,
        .asset_manifest_hash = asset_manifest_hash,
        .seed = seed,
    };
    // Force overflow/anchor validation at the compiler boundary.
    (void)descriptor.decode_range();
    return descriptor;
}

} // namespace chronon3d::graph
