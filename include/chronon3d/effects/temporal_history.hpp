#pragma once

#include <chronon3d/core/types/frame.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace chronon3d::effects {

enum class TemporalHistoryPolicy : std::uint8_t {
    FixedWindow,     // History is determined by fixed window of N preceding frames
    PrerollRequired  // Requires deterministic pre-roll of N frames from keyframe/scene start
};

/// Shared authority for temporal persistence, phosphor decay, and ghosting.
/// Prevents hidden temporal state in executors and guarantees random-access determinism.
struct TemporalHistoryConfig {
    TemporalHistoryPolicy policy{TemporalHistoryPolicy::FixedWindow};
    std::uint32_t required_history_frames{4}; // Frame count N required for deterministic evaluation
    float persistence_decay{0.82f};          // Frame-to-frame attenuation [0, 1]
    float intensity{1.0f};

    [[nodiscard]] bool validate() const noexcept {
        return required_history_frames <= 64 &&
               std::isfinite(persistence_decay) &&
               persistence_decay >= 0.0f && persistence_decay <= 1.0f &&
               std::isfinite(intensity) && intensity >= 0.0f;
    }

    /// Calculates deterministic blend weight of a historical frame offset delta_frames <= required_history_frames
    [[nodiscard]] float weight_at_offset(std::uint32_t delta_frames) const noexcept {
        if (delta_frames == 0) return 1.0f;
        if (delta_frames > required_history_frames) return 0.0f;
        return intensity * std::pow(persistence_decay, static_cast<float>(delta_frames));
    }
};

} // namespace chronon3d::effects
