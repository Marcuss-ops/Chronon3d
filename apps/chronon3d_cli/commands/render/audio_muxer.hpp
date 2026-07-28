#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli {

class AudioMuxer {
public:
    bool mux(const std::string& video_path,
             const std::vector<render_plan::AudioTrackPlan>& tracks,
             const std::string& assets_root) const;
};

}  // namespace chronon3d::cli
