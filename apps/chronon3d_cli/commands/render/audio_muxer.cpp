#include "audio_muxer.hpp"

#ifdef CHRONON3D_ENABLE_VIDEO
#include "src/media/video/mux_plan.hpp"
#endif

#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <utility>

namespace chronon3d::cli {

bool AudioMuxer::mux(const std::string& video_path,
                     const std::vector<render_plan::AudioTrackPlan>& tracks,
                     const chronon3d::assets::AssetResolver& resolver,
                     chronon3d::CancellationToken* cancellation) const {
    if (tracks.empty()) return true;
#ifndef CHRONON3D_ENABLE_VIDEO
    (void)video_path;
    (void)tracks;
    (void)resolver;
    (void)cancellation;
    spdlog::error("External audio mux requires CHRONON3D_ENABLE_VIDEO");
    return false;
#else

    media::video::MuxPlan plan;
    plan.video_input = video_path;
    plan.output = video_path;
    plan.tracks.reserve(tracks.size());

    for (const auto& track : tracks) {
        if (track.source.empty()) {
            spdlog::error("Audio track is missing source");
            return false;
        }
        const auto resolved = resolver.resolve_logical(track.source);
        if (!resolved) {
            spdlog::error("Audio track is not a resolvable logical asset: {}", track.source);
            return false;
        }
        plan.tracks.push_back(media::video::MuxAudioTrack{
            .source = *resolved,
            .volume = track.volume,
            .start_time_offset = track.start_time_offset,
            .duration_seconds = track.duration_seconds,
            .role = track.role,
            .loop = track.loop,
            .fade_in_seconds = track.fade_in_seconds,
            .fade_out_seconds = track.fade_out_seconds,
            .ducking_enabled = track.ducking_enabled,
        });
    }

    const auto result = media::video::ExternalAudioMuxer{}.run(plan, cancellation);
    if (!result) {
        spdlog::error("External audio mux failed: {}", result.error().message);
        return false;
    }
    return true;
#endif
}

}  // namespace chronon3d::cli
