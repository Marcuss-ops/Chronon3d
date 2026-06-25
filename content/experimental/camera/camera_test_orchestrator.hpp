#pragma once

#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/model/camera/camera_shot_profile.hpp>
#include <vector>
#include <string>

namespace chronon3d::content::two_point_five_d {

// State structs for orchestrator tracking
struct JerkState {
    Vec3 prev_pos[3]{};
    float max_jerk{0.0f};
    int count{0};
};

struct JitterState {
    Vec2 prev_screen_pos{0.0f, 0.0f};
    Vec2 mean_velocity{0.0f, 0.0f};
    float max_dev{0.0f};
    int count{0};
};

Scene camera_test_orchestrator(
    const FrameContext& ctx, 
    SceneBuilder& s, 
    const CameraShotProfile& shot, 
    const std::string& comp_name,
    const std::vector<std::string>& fit_layers,
    const std::vector<int>& report_frames = {0, 30, 45, 60, 90}
);

} // namespace chronon3d::content::two_point_five_d
