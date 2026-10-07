#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d::compiler {

enum class TemporalRate : std::uint8_t {
    Static,
    Cue,
    KeyframeInterval,
    PerFrame,
    SubFrame,
};

struct TemporalPropertyUpdate {
    std::string path;
    TemporalRate rate{TemporalRate::Static};
    std::int64_t active_first{0};
    std::int64_t active_end{0};
    std::vector<std::int64_t> frames;

    [[nodiscard]] bool dirty(double frame) const noexcept {
        if (!std::isfinite(frame) ||
            frame < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            frame >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return true;
        if (rate == TemporalRate::PerFrame || rate == TemporalRate::SubFrame)
            return frame >= static_cast<double>(active_first) &&
                   frame < static_cast<double>(active_end);
        if (rate == TemporalRate::Static) return frame == 0.0;
        if (rate == TemporalRate::KeyframeInterval &&
            frame != static_cast<double>(static_cast<std::int64_t>(frame)))
            return frame >= static_cast<double>(active_first) &&
                   frame < static_cast<double>(active_end);
        if (frame < static_cast<double>(active_first) ||
            frame >= static_cast<double>(active_end))
            return rate == TemporalRate::Cue &&
                std::binary_search(frames.begin(), frames.end(),
                                   static_cast<std::int64_t>(frame));
        return std::binary_search(frames.begin(), frames.end(),
                                  static_cast<std::int64_t>(frame));
    }
};

/// Compiler-owned invalidation schedule for one layer binding.
/// Sparse rates keep only their change events; dense rates use the active
/// interval instead of storing one integer per rendered frame.
struct TemporalNodeUpdate {
    std::uint32_t layer_index{0};
    TemporalRate rate{TemporalRate::Static};
    std::int64_t active_first{0};
    std::int64_t active_end{0};
    std::vector<std::int64_t> frames;
    std::vector<TemporalPropertyUpdate> properties;

    [[nodiscard]] bool dirty(double frame) const noexcept {
        if (!std::isfinite(frame) ||
            frame < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            frame >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
            return true;
        if (rate == TemporalRate::PerFrame || rate == TemporalRate::SubFrame)
            return frame >= static_cast<double>(active_first) &&
                   frame < static_cast<double>(active_end);
        if (rate == TemporalRate::Static)
            return frame == 0.0;
        if (rate == TemporalRate::KeyframeInterval &&
            frame != static_cast<double>(static_cast<std::int64_t>(frame)))
            return frame >= static_cast<double>(active_first) &&
                   frame < static_cast<double>(active_end);
        if (frame < static_cast<double>(active_first) ||
            frame >= static_cast<double>(active_end)) {
            // A cue's end event is the sole scheduled update outside its
            // half-open active interval.
            return rate == TemporalRate::Cue &&
                std::binary_search(frames.begin(), frames.end(),
                                   static_cast<std::int64_t>(frame));
        }
        return std::binary_search(frames.begin(), frames.end(),
                                  static_cast<std::int64_t>(frame));
    }
};

/// Immutable update schedule emitted by the RenderPlan compiler and consumed
/// by TypedProgramSampler/GraphExecutor. An unknown binding is conservative.
struct TemporalUpdateSchedule {
    std::vector<TemporalNodeUpdate> nodes;

    [[nodiscard]] bool dirty(std::uint32_t layer_index, double frame) const noexcept {
        return layer_index >= nodes.size() || nodes[layer_index].dirty(frame);
    }
};

} // namespace chronon3d::compiler
