#include "mux_plan.hpp"

#include "process_runner.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::media::video {
namespace {

using Args = std::vector<std::string>;

// Quote a path for the POSIX shell wrapper used to redirect ffprobe's stdout.
// Single-quote every byte and escape embedded single quotes so a job-supplied
// path cannot alter the command.
std::string shell_quote(std::string_view value) {
    std::string quoted{"'"};
    for (const char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += '\'';
    return quoted;
}

MuxError make_error(MuxErrorCode code, std::string message, int exit_code = -1) {
    return MuxError{code, std::move(message), exit_code};
}

bool finite_non_negative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

double total_audio_duration(const std::vector<MuxAudioTrack>& tracks) {
    double max_end = 0.0;
    for (const auto& track : tracks) {
        if (track.duration_seconds > 0.0) {
            max_end = std::max(max_end,
                               track.start_time_offset + track.duration_seconds);
        }
    }
    return max_end;
}

Result<Args, MuxError> build_mux_args(const MuxPlan& plan,
                                      const std::filesystem::path& temp_output) {
    if (plan.video_input.empty() || plan.output.empty() || plan.tracks.empty()) {
        return make_error(MuxErrorCode::InvalidPlan,
                          "MuxPlan requires video_input, output, and at least one audio track");
    }
    const auto expected_extension = plan.container == MuxContainer::WebM
        ? ".webm" : (plan.container == MuxContainer::Mkv ? ".mkv" : ".mp4");
    if (plan.output.extension() != expected_extension) {
        return make_error(MuxErrorCode::InvalidPlan,
                          "MuxPlan output extension does not match its container policy");
    }
    if (plan.process_timeout.count() <= 0 || plan.graceful_cancel_timeout.count() <= 0) {
        return make_error(MuxErrorCode::InvalidPlan,
                          "MuxPlan process timeouts must be positive");
    }
    if (!std::filesystem::is_regular_file(plan.video_input)) {
        return make_error(MuxErrorCode::InputMissing,
                          "video input does not exist: " + plan.video_input.string());
    }
    for (const auto& track : plan.tracks) {
        if (track.source.empty() || !std::filesystem::is_regular_file(track.source)) {
            return make_error(MuxErrorCode::InputMissing,
                              "audio input does not exist: " + track.source.string());
        }
        if (!finite_non_negative(track.volume) ||
            !finite_non_negative(track.start_time_offset) ||
            !finite_non_negative(track.duration_seconds) ||
            !finite_non_negative(track.fade_in_seconds) ||
            !finite_non_negative(track.fade_out_seconds)) {
            return make_error(MuxErrorCode::InvalidPlan,
                              "audio track contains a non-finite or negative value");
        }
    }

    Args command{"ffmpeg", "-hide_banner", "-loglevel", "error"};
    command.emplace_back(plan.overwrite ? "-y" : "-n");
    command.insert(command.end(), {"-i", plan.video_input.string()});
    for (const auto& track : plan.tracks) {
        if (track.loop) command.insert(command.end(), {"-stream_loop", "-1"});
        command.insert(command.end(), {"-i", track.source.string()});
    }
    command.insert(command.end(), {"-map", "0:v:0"});

    const double total_duration = total_audio_duration(plan.tracks);
    std::string filter;
    std::vector<std::size_t> voiceovers;
    for (std::size_t index = 0; index < plan.tracks.size(); ++index) {
        const auto& track = plan.tracks[index];
        if (track.role == "voiceover") voiceovers.push_back(index);
        filter += "[" + std::to_string(index + 1) + ":a]volume=" +
                  std::to_string(track.volume);
        if (track.start_time_offset > 0.0) {
            filter += ",adelay=" + std::to_string(static_cast<long long>(
                track.start_time_offset * 1000.0));
        }
        if (track.fade_in_seconds > 0.0) {
            filter += ",afade=t=in:d=" + std::to_string(track.fade_in_seconds);
        }
        double trim_duration = track.duration_seconds;
        if (trim_duration <= 0.0 && track.loop && total_duration > 0.0)
            trim_duration = total_duration;
        if (trim_duration > 0.0)
            filter += ",atrim=duration=" + std::to_string(trim_duration);
        if (track.fade_out_seconds > 0.0 && trim_duration > track.fade_out_seconds) {
            filter += ",afade=t=out:st=" + std::to_string(
                trim_duration - track.fade_out_seconds) +
                ":d=" + std::to_string(track.fade_out_seconds);
        }
        filter += "[track" + std::to_string(index) + "];";
    }

    for (const auto index : voiceovers) {
        filter += "[track" + std::to_string(index) + "]asplit[voice_mix" +
                  std::to_string(index) + "][voice_side" + std::to_string(index) + "];";
    }
    for (std::size_t index = 0; index < plan.tracks.size(); ++index) {
        const auto& track = plan.tracks[index];
        const bool is_voiceover = track.role == "voiceover";
        if (track.ducking_enabled && track.role == "background_music" && !voiceovers.empty()) {
            filter += "[track" + std::to_string(index) + "][voice_side" +
                      std::to_string(voiceovers.front()) +
                      "]sidechaincompress=threshold=0.1:ratio=4:attack=5:release=50[mix" +
                      std::to_string(index) + "];";
        } else if (is_voiceover) {
            filter += "[voice_mix" + std::to_string(index) + "]anull[mix" +
                      std::to_string(index) + "];";
        } else {
            filter += "[track" + std::to_string(index) + "]anull[mix" +
                      std::to_string(index) + "];";
        }
    }

    for (std::size_t index = 0; index < plan.tracks.size(); ++index)
        filter += "[mix" + std::to_string(index) + "]";
    if (plan.tracks.size() == 1) {
        filter += "anull[aout]";
    } else {
        filter += "amix=inputs=" + std::to_string(plan.tracks.size()) +
                  ":duration=longest:dropout_transition=0[aout]";
    }

    const char* audio_codec = plan.container == MuxContainer::WebM ? "libopus" : "aac";
    command.insert(command.end(), {"-filter_complex", filter, "-map", "[aout]",
                                   "-c:v", "copy", "-c:a", audio_codec, "-shortest"});
    if (plan.container == MuxContainer::Mp4)
        command.insert(command.end(), {"-movflags", "+faststart"});
    command.push_back(temp_output.string());
    return command;
}

std::string stderr_suffix(ProcessRunner& process) {
    const auto text = process.consume_stderr();
    return text.empty() ? std::string{} : " — stderr: " + text;
}

Result<double, MuxError> verify_muxed_artifact(
    const std::filesystem::path& path,
    std::size_t expected_audio_streams,
    std::chrono::milliseconds timeout,
    CancellationToken* cancellation) {
    const auto stream_probe_output = path.string() + ".probe.streams.txt";
    ProcessRunner probe;
    // ffprobe 4.4 (still present on supported worker images) has no `-o`
    // option. Use stdout redirection instead of depending on a newer ffprobe.
    const std::string command_line =
        "exec ffprobe -v error -show_entries stream=codec_type,duration "
        "-of csv=p=0 " +
        shell_quote(path.string()) + " > " + shell_quote(stream_probe_output);
    Args command{"/bin/sh", "-c", command_line};
    if (!probe.launch(command.front(), command)) {
        return make_error(MuxErrorCode::FfprobeNotFound,
                          "failed to launch ffprobe — is ffprobe on PATH?");
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int exit_code = -2;
    for (;;) {
        if (cancellation && cancellation->is_cancelled()) {
            probe.terminate_and_wait(std::chrono::seconds(2));
            std::error_code ignored;
            std::filesystem::remove(stream_probe_output, ignored);
            return make_error(MuxErrorCode::Cancelled, "audio artifact verification cancelled");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) break;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        exit_code = probe.wait_for(std::min(remaining, std::chrono::milliseconds(25)));
        if (exit_code != -2) break;
    }
    if (exit_code == -2) {
        probe.terminate_and_wait(std::chrono::seconds(2));
        const auto detail = stderr_suffix(probe);
        std::error_code ignored;
        std::filesystem::remove(stream_probe_output, ignored);
        return make_error(MuxErrorCode::Timeout, "ffprobe verification timed out" + detail);
    }
    if (exit_code != 0) {
        const auto detail = stderr_suffix(probe);
        std::error_code ignored;
        std::filesystem::remove(stream_probe_output, ignored);
        return make_error(MuxErrorCode::VerificationFailed,
                          "ffprobe rejected muxed artifact" + detail, exit_code);
    }

    std::ifstream input(stream_probe_output);
    std::size_t video_streams = 0;
    std::size_t audio_streams = 0;
    double video_duration = 0.0;
    double audio_duration = 0.0;
    std::string line;
    while (std::getline(input, line)) {
        const auto comma = line.find(',');
        const auto kind = line.substr(0, comma);
        double duration = 0.0;
        if (comma != std::string::npos) {
            try { duration = std::stod(line.substr(comma + 1)); }
            catch (...) { duration = 0.0; }
        }
        if (kind == "video") {
            ++video_streams;
            video_duration = std::max(video_duration, duration);
        } else if (kind == "audio") {
            ++audio_streams;
            audio_duration = std::max(audio_duration, duration);
        }
    }
    std::error_code ignored;
    std::filesystem::remove(stream_probe_output, ignored);
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error) ||
        std::filesystem::file_size(path, file_error) == 0) {
        return make_error(MuxErrorCode::VerificationFailed,
                          "muxed artifact is missing or empty");
    }
    if (video_streams != 1 || audio_streams != expected_audio_streams) {
        return make_error(MuxErrorCode::VerificationFailed,
                          "muxed artifact stream contract failed: video=" +
                              std::to_string(video_streams) + ", audio=" +
                              std::to_string(audio_streams));
    }
    if (!(video_duration > 0.0) || !(audio_duration > 0.0) ||
        std::abs(video_duration - audio_duration) > 0.15) {
        return make_error(MuxErrorCode::VerificationFailed,
                          "muxed artifact audio/video duration mismatch");
    }
    return video_duration;
}

}  // namespace

Result<MuxReport, MuxError> ExternalAudioMuxer::run(
    const MuxPlan& plan, CancellationToken* cancellation) const {
    MuxPlan effective_plan = plan;
    if (effective_plan.output.extension() == ".webm") effective_plan.container = MuxContainer::WebM;
    else if (effective_plan.output.extension() == ".mkv") effective_plan.container = MuxContainer::Mkv;
    else if (effective_plan.output.extension() == ".mp4") effective_plan.container = MuxContainer::Mp4;
    else {
        return make_error(MuxErrorCode::InvalidPlan,
                          "MuxPlan output must use .mp4, .mkv, or .webm");
    }
    if (!effective_plan.overwrite && std::filesystem::exists(effective_plan.output)) {
        return make_error(MuxErrorCode::OutputExists,
                          "mux output already exists: " + effective_plan.output.string());
    }

    const auto temp = effective_plan.output.string() + ".chronon.audio.partial" +
        effective_plan.output.extension().string();
    std::error_code cleanup_error;
    std::filesystem::remove(temp, cleanup_error);
    auto args = build_mux_args(effective_plan, temp);
    if (!args) return args.error();

    if (cancellation && cancellation->is_cancelled()) {
        std::filesystem::remove(temp, cleanup_error);
        return make_error(MuxErrorCode::Cancelled, "audio mux cancelled before execution");
    }

    ProcessRunner ffmpeg;
    if (!ffmpeg.launch(args->front(), *args)) {
        return make_error(MuxErrorCode::FfmpegNotFound,
                          "failed to launch ffmpeg — is ffmpeg on PATH?");
    }
    const auto deadline = std::chrono::steady_clock::now() + effective_plan.process_timeout;
    for (;;) {
        if (cancellation && cancellation->is_cancelled()) {
            ffmpeg.terminate_and_wait(effective_plan.graceful_cancel_timeout);
            std::filesystem::remove(temp, cleanup_error);
            return make_error(MuxErrorCode::Cancelled, "audio mux cancelled");
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            ffmpeg.terminate_and_wait(effective_plan.graceful_cancel_timeout);
            const auto detail = stderr_suffix(ffmpeg);
            std::filesystem::remove(temp, cleanup_error);
            return make_error(MuxErrorCode::Timeout,
                              "ffmpeg mux timed out" + detail);
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        const int exit_code = ffmpeg.wait_for(std::min(remaining, std::chrono::milliseconds(25)));
        if (exit_code == -2) continue;
        if (exit_code != 0) {
            const auto detail = stderr_suffix(ffmpeg);
            std::filesystem::remove(temp, cleanup_error);
            return make_error(MuxErrorCode::ProcessFailed,
                              "ffmpeg mux failed with exit code " +
                                  std::to_string(exit_code) + detail, exit_code);
        }
        break;
    }
    if (cancellation && cancellation->is_cancelled()) {
        std::filesystem::remove(temp, cleanup_error);
        return make_error(MuxErrorCode::Cancelled, "audio mux cancelled after execution");
    }

    auto verified = verify_muxed_artifact(
        temp, 1, effective_plan.process_timeout, cancellation);
    if (!verified) {
        std::filesystem::remove(temp, cleanup_error);
        return verified.error();
    }
    if (cancellation && cancellation->is_cancelled()) {
        std::filesystem::remove(temp, cleanup_error);
        return make_error(MuxErrorCode::Cancelled, "audio mux cancelled before publish");
    }
    std::filesystem::rename(temp, effective_plan.output, cleanup_error);
    if (cleanup_error) {
        const auto publish_message = cleanup_error.message();
        std::error_code remove_error;
        std::filesystem::remove(temp, remove_error);
        return make_error(MuxErrorCode::PublishFailed,
                          "cannot publish muxed artifact: " + publish_message);
    }

    return MuxReport{effective_plan.output, effective_plan.tracks.size(), verified.value()};
}

}  // namespace chronon3d::media::video
