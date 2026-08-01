#pragma once

#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

#include <string>
#include <vector>

namespace chronon3d::cli {

class AudioMuxer {
public:
    bool mux(const std::string& video_path,
             const std::vector<render_plan::AudioTrackPlan>& tracks,
             const chronon3d::assets::AssetResolver& resolver) const;
};

}  // namespace chronon3d::cli
