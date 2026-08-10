#pragma once

#include <chronon3d/core/types/sample_time.hpp>
#include <chronon3d/scene/model/layer/time_remap.hpp>

#include <cmath>

namespace chronon3d {

// ── LayerTimeResolver — single source of truth for layer-local time ─────────
//
// Centralises offset/freeze/animated-time-remap/reverse/speed logic for
// Layer::local_time() and the layer activity boundary.
// Every layer time-remap path in the engine should route through here.

struct LayerTimeResolver {
    [[nodiscard]] static bool active_at(
        Frame frame,
        Frame layer_start,
        Frame layer_duration) noexcept {
        if (frame < layer_start) return false;
        if (layer_duration < Frame{0}) return true;
        return frame < layer_start + layer_duration;
    }

    [[nodiscard]] static SampleTime resolve(
        SampleTime global_time,
        Frame layer_start,
        Frame time_offset,
        Frame layer_duration,
        const TimeRemap& remap)
    {
        const double raw_frame = global_time.frame
            - static_cast<double>(layer_start)
            + static_cast<double>(time_offset);

        if (!remap.active()) {
            return SampleTime::from_frame(raw_frame, global_time.frame_rate);
        }

        if (remap.freeze_frame >= 0) {
            return SampleTime::from_frame_int(remap.freeze_frame, global_time.frame_rate);
        }

        if (remap.time_remap.is_time_dependent()) {
            const double mapped = static_cast<double>(remap.time_remap.evaluate(
                SampleTime::from_frame(raw_frame, global_time.frame_rate)));
            return SampleTime::from_frame(mapped, global_time.frame_rate);
        }

        double scaled;
        if (remap.speed < 0.0f && static_cast<double>(layer_duration) > 0.0) {
            scaled = static_cast<double>(layer_duration)
                   + static_cast<double>(remap.speed) * raw_frame;
        } else {
            scaled = raw_frame * static_cast<double>(remap.speed);
        }
        return SampleTime::from_frame(scaled, global_time.frame_rate);
    }
};

} // namespace chronon3d
