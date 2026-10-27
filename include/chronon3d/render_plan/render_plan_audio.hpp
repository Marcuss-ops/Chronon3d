#pragma once

#include <chronon3d/render_plan/render_plan_types.hpp>

#include <string>
#include <vector>

namespace chronon3d::render_plan {

struct AudioClipPlan {
    std::string id;
    std::string asset;
    std::optional<Frame> clip_in;
    std::optional<Frame> clip_out;
    std::optional<Frame> start;
    float gain_db{0.0f};
    bool muted{false};
    std::optional<AnimationTrackPlan> gain;
    std::string role{"music"};
};

struct AudioMixPlan {
    std::vector<AudioClipPlan> clips;
    float master_gain_db{0.0f};
    float duck_amount_db{-9.0f};
    float duck_attack_ms{120.0f};
    float duck_release_ms{400.0f};
};


} // namespace chronon3d::render_plan
