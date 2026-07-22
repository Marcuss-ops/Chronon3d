#pragma once

#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/animation/core/animated_value.hpp>

namespace chronon3d {

// ── Time Remap (AE-4) ────────────────────────────────────────────────────────
// Per-layer time control: speed, reverse, freeze frame, and animated time remap.
// When active (speed != 1.0 || reverse || time_remap is animated || freeze_frame >= 0),
// Layer::local_time() remaps the composition SampleTime to a layer-local SampleTime.

struct TimeRemap {
    f32  speed{1.0f};          // playback speed multiplier (0.5 = half-speed, 2.0 = double, -1.0 = reverse)
    Frame freeze_frame{-1};    // if >= 0, hold this source frame forever
    AnimatedValue<f32> time_remap;  // maps comp frame → source frame (in frames). When animated, overrides speed.

    [[nodiscard]] bool active() const {
        return speed != 1.0f || freeze_frame >= 0 || time_remap.is_time_dependent();
    }
};

} // namespace chronon3d
