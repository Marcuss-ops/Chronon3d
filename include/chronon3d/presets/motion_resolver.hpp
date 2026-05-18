#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <chronon3d/core/frame_context.hpp>

namespace chronon3d::presets::motion {

[[nodiscard]] MotionState resolve_motion_state(const FrameContext& ctx, const MotionObject& obj);

} // namespace chronon3d::presets::motion
