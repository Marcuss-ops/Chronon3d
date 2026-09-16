#include "copy_lane_coverage.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <utility>

namespace chronon3d::cli {
namespace {

namespace rp = chronon3d::render_plan;

/// Output frame rate used to turn authored frame windows into the source-time
/// seconds a `ChangedInterval` speaks. The plan's canvas is the authority; a
/// non-positive or non-finite rate cannot be interpreted at all, so it is
/// reported by refusing the job instead of silently defaulting to 30 (a wrong
/// default would compare the overlay window against the wrong timeline and
/// could turn a real under-declaration into a pass).
[[nodiscard]] double output_fps(const rp::RenderPlan& plan) noexcept {
    const auto fps = plan.canvas.fps.fps();
    if (!std::isfinite(fps) || fps <= 0.0) return 0.0;
    return fps;
}

struct LayerWindow {
    bool draws{false};
    bool unbounded{false};
    double first_second{0.0};
    double last_second{0.0};
};

/// The window in which `layer` draws, in output (source-time) seconds.
///
/// Two spellings mean UNBOUNDED in this pipeline, not zero-length: an absent
/// `duration_frames`, and `duration_frames == 0`. The render path keeps a layer
/// active from its start to the end of the composition in both cases
/// (`direct_yuv_program.cpp`: `duration > 0 && frame >= from + duration`), so
/// reading 0 as "draws nothing" would let a copy-only job slip through with a
/// layer that in fact spans the whole export.
[[nodiscard]] LayerWindow layer_window(const rp::RenderPlan& plan,
                                       const rp::LayerPlan& layer) noexcept {
    LayerWindow window;
    const std::int64_t canvas_end =
        std::max<std::int64_t>(plan.canvas.duration.integral(), 0);
    const std::int64_t first = layer.start_frame
        ? std::max<std::int64_t>(layer.start_frame->integral(), 0) : 0;
    const double fps = output_fps(plan);

    // An unprovable frame rate or timeline cannot be shown to lie outside the
    // layer's window, so the layer is treated as drawing everything: fail
    // closed, and let the refusal message say which declaration is missing.
    if (fps <= 0.0 || canvas_end <= 0) {
        window.draws = true;
        window.unbounded = true;
        window.first_second = fps > 0.0 ? static_cast<double>(first) / fps : 0.0;
        window.last_second = std::numeric_limits<double>::infinity();
        return window;
    }
    if (first >= canvas_end) return window;  // starts after the timeline ends

    const std::int64_t requested = layer.duration_frames
        ? layer.duration_frames->integral() : 0;
    const std::int64_t last = requested > 0
        ? std::min(first + requested, canvas_end) : canvas_end;
    if (last <= first) return window;
    window.draws = true;
    window.first_second = static_cast<double>(first) / fps;
    window.last_second = static_cast<double>(last) / fps;
    return window;
}

[[nodiscard]] bool layer_can_draw(const rp::LayerPlan& layer) noexcept {
    if (layer.opacity && *layer.opacity <= 0.0f) return false;
    if (layer.type == rp::LayerType::Text && layer.text.empty()) return false;
    return true;
}

[[nodiscard]] std::string format_seconds(double value) {
    if (!std::isfinite(value)) return "the end of an unknown timeline";
    std::ostringstream os;
    os.precision(6);
    os << std::fixed << value << "s";
    return os.str();
}

/// `content` intervals only: a `trim` interval changes the published window,
/// not the pixels, so it never licenses a copy of altered content.
[[nodiscard]] std::vector<std::pair<double, double>> merged_content_spans(
    std::span<const media::ChangedInterval> changed_intervals) {
    std::vector<std::pair<double, double>> spans;
    spans.reserve(changed_intervals.size());
    for (const auto& interval : changed_intervals) {
        if (interval.kind != media::ChangedIntervalKind::Content) continue;
        spans.emplace_back(interval.start_seconds, interval.end_seconds);
    }
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto& span : spans) {
        if (!merged.empty() && span.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, span.second);
            continue;
        }
        merged.push_back(span);
    }
    return merged;
}

/// First gap between `window` and the union of `spans`, or nothing when the
/// union covers the window. Exact in source-time seconds: the producer declares
/// whole intervals, so a boundary that does not reach the layer's window is a
/// real under-declaration, not a rounding artefact.
struct Gap {
    double first_second{0.0};
    double second_second{0.0};
};

[[nodiscard]] std::vector<Gap> uncovered_gaps(
    const LayerWindow& window,
    std::span<const std::pair<double, double>> spans) {
    std::vector<Gap> gaps;
    double cursor = window.first_second;
    for (const auto& span : spans) {
        if (cursor >= window.last_second) break;
        if (span.second <= cursor) continue;
        if (span.first > cursor) {
            gaps.push_back(Gap{cursor, std::min(span.first, window.last_second)});
        }
        cursor = std::max(cursor, span.second);
    }
    if (cursor < window.last_second) {
        gaps.push_back(Gap{cursor, window.last_second});
    }
    return gaps;
}

[[nodiscard]] std::string describe_layer(const rp::LayerPlan& layer) {
    std::string kind;
    switch (layer.type) {
        case rp::LayerType::Text: kind = "text"; break;
        case rp::LayerType::Image: kind = "image"; break;
        case rp::LayerType::Video: kind = "video"; break;
        case rp::LayerType::Color: kind = "color"; break;
    }
    return kind + " layer '" + layer.id + "'";
}

} // namespace

bool layer_is_additive(const rp::RenderPlan& plan, std::size_t index) noexcept {
    if (index >= plan.layers.size()) return false;
    const auto& layer = plan.layers[index];
    if (!layer_can_draw(layer)) return false;
    // A layer whose window does not touch the timeline draws nothing, so it
    // cannot make a copy-only job publish the wrong pixels.
    if (!layer_window(plan, layer).draws) return false;
    if (layer.type != rp::LayerType::Video) return true;
    // The first video layer is the base the caller is copying. Anything after
    // it is composited content the source does not contain.
    for (std::size_t probe = 0; probe < index; ++probe) {
        if (plan.layers[probe].type == rp::LayerType::Video &&
            layer_can_draw(plan.layers[probe])) {
            return true;
        }
    }
    return false;
}

CopyLaneCoverageVerdict validate_copy_lane_coverage(
    const rp::RenderPlan& plan,
    bool gop_copy_only,
    std::span<const media::ChangedInterval> changed_intervals) {
    CopyLaneCoverageVerdict verdict;

    std::vector<std::size_t> additive;
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        if (layer_is_additive(plan, index)) additive.push_back(index);
    }
    if (additive.empty()) return verdict;

    if (gop_copy_only) {
        std::ostringstream os;
        os << "gop_copy_only publishes the compressed source verbatim, but plan '"
           << plan.job_id << "' draws " << additive.size()
           << " layer(s) no packet copy can contain (first: "
           << describe_layer(plan.layers[additive.front()])
           << "). Drop --gop-copy-only (and gop_copy_only in the IPC payload) so "
              "the overlay is rendered, or remove the layers from the plan.";
        verdict.ok = false;
        verdict.message = os.str();
        for (const auto index : additive) {
            const auto window = layer_window(plan, plan.layers[index]);
            verdict.uncovered.push_back(CopyLaneLayerWindow{
                plan.layers[index].id, window.first_second, window.last_second});
        }
        return verdict;
    }

    // No declaration: the whole export is treated as changed and nothing is
    // copied, so there is nothing to cover. This is the legacy semantics.
    if (changed_intervals.empty()) return verdict;

    const auto spans = merged_content_spans(changed_intervals);
    for (const auto index : additive) {
        const auto& layer = plan.layers[index];
        const auto window = layer_window(plan, layer);
        if (!window.draws) continue;
        if (window.unbounded) {
            std::ostringstream os;
            os << "cannot prove copy-lane coverage for " << describe_layer(layer)
               << ": ";
            if (output_fps(plan) <= 0.0) {
                os << "the plan declares a non-positive frame rate, so authored "
                      "frame windows cannot be compared against the declared "
                      "changed intervals";
            } else {
                os << "the plan declares no canvas duration, so no finite "
                      "declaration can be proven to cover the layer";
            }
            os << ". Fix the plan (canvas.fps / canvas.duration) or drop the "
                  "interval list so the whole export is re-encoded.";
            verdict.ok = false;
            verdict.message = os.str();
            verdict.uncovered.push_back(CopyLaneLayerWindow{
                layer.id, window.first_second, window.last_second});
            return verdict;
        }
        const auto gaps = uncovered_gaps(window, spans);
        if (gaps.empty()) continue;

        std::ostringstream os;
        os << "declared changed intervals do not cover "
           << describe_layer(layer) << ": active "
           << format_seconds(window.first_second) << " to "
           << format_seconds(window.last_second) << ", undeclared ";
        for (std::size_t gap = 0; gap < gaps.size(); ++gap) {
            if (gap != 0) os << ", ";
            os << format_seconds(gaps[gap].first_second) << "-"
               << format_seconds(gaps[gap].second_second);
        }
        os << ". The copy lane would publish the base packets there and the "
              "overlay would be absent from those frames; declare the window as "
              "a content interval (--changed-interval start:end:content, or "
              "changed_intervals in the plan/IPC payload) with kind 'content', "
              "or drop the interval list so the whole export is re-encoded.";
        verdict.ok = false;
        verdict.message = os.str();
        for (const auto& gap : gaps) {
            verdict.uncovered.push_back(
                CopyLaneLayerWindow{layer.id, gap.first_second, gap.second_second});
        }
        return verdict;  // first offender: one actionable message per job
    }
    return verdict;
}

} // namespace chronon3d::cli
