#pragma once

#include <chronon3d/presets/motion_object.hpp>
#include <chronon3d/presets/motion_state.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <vector>

namespace chronon3d::presets::motion {

void draw_motion_object(SceneBuilder& s, const FrameContext& ctx, const MotionObject& obj);

void draw_motion_objects(SceneBuilder& s, const FrameContext& ctx, const std::vector<MotionObject>& objects);

} // namespace chronon3d::presets::motion
