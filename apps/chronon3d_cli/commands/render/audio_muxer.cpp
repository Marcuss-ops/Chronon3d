#include "audio_muxer.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <filesystem>
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

std::string resolve_audio_path(const std::string& source,
                               const std::string& assets_root) {
    const std::filesystem::path path(source);
    if (path.is_absolute() || assets_root.empty()) return path.string();
    return (std::filesystem::path(assets_root) / path).string();
}

}  // namespace

bool AudioMuxer::mux(const std::string& video_path,
                     const std::vector<render_plan::AudioTrackPlan>& tracks,
                     const std::string& assets_root) const {
    if (tracks.empty()) return true;

    std::vector<std::string> audio_paths;
    audio_paths.reserve(tracks.size());
    for (const auto& track : tracks) {
        if (track.source.empty()) {
            spdlog::error("Audio track is missing source");
            return false;
        }
        audio_paths.push_back(resolve_audio_path(track.source, assets_root));
    }

    const std::string temp = video_path + ".audio.tmp.mp4";
    std::vector<std::string> command{
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", video_path};
    for (const auto& path : audio_paths) command.insert(command.end(), {"-i", path});
    command.insert(command.end(), {"-map", "0:v:0"});

    std::string filter;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        filter += "[" + std::to_string(index + 1) + ":a]volume=" +
                  std::to_string(track.volume);
        if (track.start_time_offset > 0) {
            filter += ",adelay=" +
                      std::to_string(static_cast<long long>(track.start_time_offset * 1000.0)) +
                      ":all=1";
        }
        if (track.duration_seconds > 0)
            filter += ",atrim=duration=" + std::to_string(track.duration_seconds);
        filter += "[a" + std::to_string(index) + "];";
    }
    if (tracks.size() == 1) {
        filter += "[a0]anull[aout]";
    } else {
        for (std::size_t index = 0; index < tracks.size(); ++index)
            filter += "[a" + std::to_string(index) + "]";
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
