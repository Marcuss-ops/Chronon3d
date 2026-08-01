#include "audio_muxer.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <filesystem>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace chronon3d::cli {
namespace {

int run_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return -1;
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    const pid_t child = ::fork();
    if (child < 0) return -1;
    if (child == 0) {
        ::execvp(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

// Compute the total audio timeline duration from the tracks.
// Returns the maximum (start_time_offset + effective_duration) across
// all tracks, or 0.0 if no tracks have a defined duration.
double total_audio_duration(const std::vector<render_plan::AudioTrackPlan>& tracks) {
    double max_end = 0.0;
    for (const auto& track : tracks) {
        double effective = track.duration_seconds;
        // Skip tracks with no explicit duration — we can't compute
        // their end point. Looping tracks without a duration will play
        // indefinitely until -shortest stops them.
        if (effective <= 0.0) continue;
        double end = track.start_time_offset + effective;
        if (end > max_end) max_end = end;
    }
    return max_end;
}

}  // namespace

bool AudioMuxer::mux(const std::string& video_path,
                     const std::vector<render_plan::AudioTrackPlan>& tracks,
                     const chronon3d::assets::AssetResolver& resolver) const {
    if (tracks.empty()) return true;

    // Resolve audio paths.
    std::vector<std::string> audio_paths;
    audio_paths.reserve(tracks.size());
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
        audio_paths.push_back(resolved->string());
    }

    // Identify voiceover tracks (used as sidechain source for ducking).
    std::vector<std::size_t> voiceover_indices;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].role == "voiceover") {
            voiceover_indices.push_back(i);
        }
    }

    const double total_dur = total_audio_duration(tracks);
    const std::string temp = video_path + ".audio.tmp.mp4";

    // Build the FFmpeg command.
    // Input: video + all audio sources.
    std::vector<std::string> command{
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", video_path};
    for (std::size_t i = 0; i < audio_paths.size(); ++i) {
        // -stream_loop -1 for looping tracks (e.g. background_music).
        if (tracks[i].loop) {
            command.insert(command.end(), {"-stream_loop", "-1"});
        }
        command.insert(command.end(), {"-i", audio_paths[i]});
    }
    command.insert(command.end(), {"-map", "0:v:0"});

    // ── Build the filter_complex graph ──────────────────────────
    // Phase A: Per-track processing (volume, delay, trim, fade).
    //          Output labels: [at<N>] for each track.
    // Phase B: Ducking (sidechain compression) for background_music
    //          tracks that have ducking_enabled=true.
    // Phase C: Mix all tracks with amix → [aout].

    std::string filter;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        const auto stream = std::to_string(index + 1);

        // Start with this track's audio stream.
        filter += "[" + stream + ":a]";

        // Volume adjustment.
        filter += "volume=" + std::to_string(track.volume);

        // Delay (start_time_offset). A single delay value applies to
        // all channels by default in FFmpeg's adelay filter.
        if (track.start_time_offset > 0.0) {
            filter += ",adelay=" +
                      std::to_string(static_cast<long long>(track.start_time_offset * 1000.0));
        }

        // Fade-in (applied early — before trimming/looping, so it
        // fades in from silence at the beginning of the track).
        if (track.fade_in_seconds > 0.0) {
            filter += ",afade=t=in:d=" + std::to_string(track.fade_in_seconds);
        }

        // Duration trim.
        // - Looping tracks: trim to total_audio_duration OR keep
        //   track.duration_seconds if explicitly set.
        // - Non-looping tracks: trim to track.duration_seconds if set.
        double trim_duration = track.duration_seconds;
        if (trim_duration <= 0.0 && track.loop && total_dur > 0.0) {
            trim_duration = total_dur;
        }
        if (trim_duration > 0.0) {
            filter += ",atrim=duration=" + std::to_string(trim_duration);
        }

        // Fade-out.
        // - Looping tracks with atrim: we know the trimmed duration,
        //   so compute fade-out start as trim_duration - fade_out.
        // - Non-looping tracks without explicit trim: use afade=t=out
        //   without explicit st (FFmpeg fades at natural end of stream).
        if (track.fade_out_seconds > 0.0) {
            if (trim_duration > 0.0 && trim_duration > track.fade_out_seconds) {
                double fade_start = trim_duration - track.fade_out_seconds;
                filter += ",afade=t=out:st=" + std::to_string(fade_start) +
                          ":d=" + std::to_string(track.fade_out_seconds);
            } else if (!track.loop) {
                // Non-looping, no explicit duration → FFmpeg handles
                // fade-out at the natural end of the stream.
                filter += ",afade=t=out:d=" + std::to_string(track.fade_out_seconds);
            }
            // Looping without atrim: skip fade-out (can't determine
            // the end point without knowing the total video duration).
        }

        // Output label for this track (pre-ducking).
        filter += "[at" + std::to_string(index) + "];";
    }

    // Phase B: Ducking — apply sidechain compression to background_music
    // tracks using voiceover tracks as the sidechain source.
    //
    // CRITICAL: sidechaincompress consumes both inputs and produces ONE
    // output — the sidechain stream is NOT passed through. If we feed
    // [at<vo>] directly into sidechaincompress, the voiceover disappears
    // from the final amix. Instead, we split each voiceover with asplit:
    //   [at<vo>]asplit[at<vo>][vo_sc<vo>];
    // keeping [at<vo>] for the amix and using [vo_sc<vo>] as the sidechain.
    //
    // Split once per voiceover track, before any ducking operations.
    std::vector<bool> vo_split(tracks.size(), false);
    for (std::size_t vo_idx : voiceover_indices) {
        filter += "[at" + std::to_string(vo_idx) + "]asplit";
        filter += "[at" + std::to_string(vo_idx) + "]";
        filter += "[vo_sc" + std::to_string(vo_idx) + "];";
        vo_split[vo_idx] = true;
    }

    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        if (!track.ducking_enabled || track.role != "background_music") continue;
        if (voiceover_indices.empty()) continue;

        // Use the first voiceover track as the sidechain source.
        // (Multiple voiceovers can be mixed together BEFORE ducking in
        // a future enhancement.)
        std::size_t vo_idx = voiceover_indices[0];

        // [bgm_pre][vo_sc]sidechaincompress → [at<N>]
        // Overwriting [at<N>] so the amix phase picks up the ducked version.
        filter += "[at" + std::to_string(index) + "]";
        filter += "[vo_sc" + std::to_string(vo_idx) + "]";
        filter += "sidechaincompress=threshold=0.1:ratio=4:attack=5:release=50";
        filter += "[at" + std::to_string(index) + "];";
    }

    // Phase C: Mix all tracks.
    // Total input count for amix.
    std::size_t active_tracks = tracks.size();
    if (active_tracks == 1) {
        filter += "[at0]anull[aout]";
    } else {
        for (std::size_t index = 0; index < tracks.size(); ++index)
            filter += "[at" + std::to_string(index) + "]";
        filter += "amix=inputs=" + std::to_string(tracks.size()) +
                  ":duration=longest:dropout_transition=0[aout]";
    }

    command.insert(command.end(), {"-filter_complex", filter, "-map", "[aout]",
                                   "-c:v", "copy", "-c:a", "aac", "-shortest",
                                   "-movflags", "+faststart", temp});

    if (run_process(command) != 0) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        spdlog::error("FFmpeg audio mux failed");
        return false;
    }
    std::error_code error;
    std::filesystem::rename(temp, video_path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        spdlog::error("Cannot replace rendered output with audio-muxed file: {}",
                      error.message());
        return false;
    }
    return true;
}

}  // namespace chronon3d::cli
