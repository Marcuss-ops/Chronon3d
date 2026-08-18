#include <chronon3d/media/video/output_contract.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/core/profiling/profiling.hpp>

#include "process_runner.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::media::video {
namespace {

using Args = std::vector<std::string>;

// ── Minimal ffprobe JSON extraction (no nlohmann dependency) ──────────────
// ffprobe `-of json` emits `"key": value`; we extract the first occurrence of
// each key, mirroring the long-standing CLI helper precedent
// (pipe_export_helpers.cpp). The video stream is always emitted first, so
// first-occurrence lookup resolves to the video stream for codec/geometry.

std::string find_json_value(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return {};
    const auto start = json.find_first_not_of(" \t\r\n", colon + 1);
    if (start == std::string::npos) return {};
    if (json[start] == '"') {
        const auto end = json.find('"', start + 1);
        if (end == std::string::npos) return {};
        return json.substr(start + 1, end - start - 1);
    }
    const auto end = json.find_first_of(",}\r\n", start);
    if (end == std::string::npos) return {};
    return json.substr(start, end - start);
}

// Quote a path for the small POSIX shell wrapper used to redirect ffprobe's
// stdout. Single-quote every byte and escape embedded single quotes so paths
// supplied by a job cannot alter the command.
std::string shell_quote(std::string_view value) {
    std::string quoted{"'"};
    for (const char ch : value) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += '\'';
    return quoted;
}

std::size_t count_occurrences(const std::string& haystack,
                              const std::string& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

bool parse_fps(const std::string& rate, double& fps) {
    if (rate.empty()) return false;
    const auto slash = rate.find('/');
    try {
        if (slash == std::string::npos) {
            fps = std::stod(rate);
        } else {
            const double num = std::stod(rate.substr(0, slash));
            const double den = std::stod(rate.substr(slash + 1));
            if (den <= 0.0) return false;
            fps = num / den;
        }
        return fps > 0.0;
    } catch (...) {
        return false;
    }
}

bool parse_i64(const std::string& value, std::int64_t& out) {
    if (value.empty()) return false;
    try {
        out = std::stoll(value);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_double(const std::string& value, double& out) {
    if (value.empty()) return false;
    try {
        out = std::stod(value);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

Result<OutputContract, std::string> resolve_output_contract(
    std::string_view profile_id) {
    if (profile_id == "youtube_overlay_v1") {
        OutputContract contract;
        contract.width = 1920;
        contract.height = 1080;
        contract.fps = chronon3d::FrameRate{30, 1};
        contract.video_codec = "h264";
        contract.pixel_format = "yuv420p";
        contract.audio_required = true;
        contract.audio_streams = 1;
        contract.frame_count = -1;
        return contract;
    }
    return std::string("unknown output profile: ") + std::string(profile_id);
}

OutputVerificationResult verify_output_contract(
    const std::filesystem::path& artifact,
    const OutputContract& contract) {
    OutputVerificationResult result;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(artifact, ec) ||
        std::filesystem::file_size(artifact, ec) == 0) {
        result.failure = "artifact missing or empty: " + artifact.string();
        return result;
    }

    // ── ffprobe: streams + format → temp JSON ─────────────────────────────
    const auto json_path = artifact.string() + ".chronon.probe.json";
    std::error_code ignored;
    std::filesystem::remove(json_path, ignored);

    ProcessRunner probe;
    // ffprobe 4.4 (still present on supported worker images) has no `-o`
    // option. Use stdout redirection instead of depending on a newer ffprobe.
    const std::string command_line =
        "exec ffprobe -v error -show_streams -show_format -of json " +
        shell_quote(artifact.string()) + " > " + shell_quote(json_path);
    Args command{"/bin/sh", "-c", command_line};
    const auto ffprobe_t0 = profiling::now();
    if (!probe.launch(command.front(), command)) {
        result.ffprobe_ms = profiling::elapsed_ms(ffprobe_t0);
        std::filesystem::remove(json_path, ignored);
        result.ffprobe_missing = true;
        result.failure = "ffprobe not available on PATH";
        return result;
    }
    const int exit_code = probe.wait_for(std::chrono::seconds(30));
    result.ffprobe_ms = profiling::elapsed_ms(ffprobe_t0);
    if (exit_code != 0) {
        const auto probe_stderr = probe.consume_stderr();
        std::filesystem::remove(json_path, ignored);
        result.failure = exit_code == -2
            ? "ffprobe verification timed out"
            : "ffprobe rejected artifact (exit " + std::to_string(exit_code) + ")" +
              (probe_stderr.empty() ? std::string{} : ": " + probe_stderr);
        return result;
    }

    std::ifstream input(json_path);
    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();
    std::filesystem::remove(json_path, ignored);
    if (json.empty()) {
        result.failure = "ffprobe produced no output";
        return result;
    }

    // ── Stream facts ──────────────────────────────────────────────────────
    const std::size_t video_streams =
        count_occurrences(json, "\"codec_type\": \"video\"");
    const std::size_t audio_streams =
        count_occurrences(json, "\"codec_type\": \"audio\"");
    result.audio_streams = audio_streams;

    std::int64_t parsed = 0;
    if (parse_i64(find_json_value(json, "width"), parsed)) {
        result.width = static_cast<int>(parsed);
    }
    if (parse_i64(find_json_value(json, "height"), parsed)) {
        result.height = static_cast<int>(parsed);
    }
    result.video_codec = find_json_value(json, "codec_name");
    result.pixel_format = find_json_value(json, "pix_fmt");

    double fps = 0.0;
    if (parse_fps(find_json_value(json, "r_frame_rate"), fps)) {
        result.fps = fps;
    }
    double duration = 0.0;
    if (parse_double(find_json_value(json, "duration"), duration)) {
        result.duration_seconds = duration;
    }

    std::int64_t observed_frames = 0;
    if (parse_i64(find_json_value(json, "nb_frames"), observed_frames) &&
        observed_frames > 0) {
        result.frame_count = observed_frames;
    } else if (duration > 0.0 && result.fps > 0.0) {
        // h264 MP4 often reports nb_frames=N/A; derive from duration × fps.
        result.frame_count =
            static_cast<std::int64_t>(std::llround(duration * result.fps));
    }

    // ── Structural verdict (`passed`) ─────────────────────────────────────
    if (video_streams != 1) {
        result.failure = "expected 1 video stream, found " +
                         std::to_string(video_streams);
        return result;
    }
    if (result.width != contract.width || result.height != contract.height) {
        result.failure = "resolution mismatch: got " +
                         std::to_string(result.width) + "x" +
                         std::to_string(result.height) + ", expected " +
                         std::to_string(contract.width) + "x" +
                         std::to_string(contract.height);
        return result;
    }
    const double expected_fps = contract.fps.fps();
    if (result.fps <= 0.0 ||
        std::abs(result.fps - expected_fps) > 0.05) {
        result.failure = "frame rate mismatch: got " +
                         std::to_string(result.fps) + ", expected " +
                         std::to_string(expected_fps);
        return result;
    }
    if (duration <= 0.0) {
        result.failure = "duration is not positive";
        return result;
    }

    // ── Media contract (`copy_eligible`) ──────────────────────────────────
    result.passed = true;

    // Compute the SHA-256 digest for every decodable artifact (the durable
    // content fingerprint used for reporting and copy eligibility).  It runs
    // before the media-contract verdict so a contract mismatch still reports
    // the digest instead of an empty placeholder.
    const auto sha256_t0 = profiling::now();
    const auto digest = chronon3d::assets::sha256_file(artifact);
    result.sha256_ms = profiling::elapsed_ms(sha256_t0);
    if (!digest) {
        result.passed = false;
        result.failure = "cannot compute SHA-256 for artifact";
        return result;
    }
    result.sha256 = digest->hex();

    bool contract_ok = true;
    if (result.video_codec != contract.video_codec) {
        contract_ok = false;
        result.failure = "codec mismatch: got " + result.video_codec +
                         ", expected " + contract.video_codec;
    } else if (result.pixel_format != contract.pixel_format) {
        contract_ok = false;
        result.failure = "pixel format mismatch: got " + result.pixel_format +
                         ", expected " + contract.pixel_format;
    } else if (contract.audio_required &&
               audio_streams != contract.audio_streams) {
        contract_ok = false;
        result.failure = "audio stream mismatch: got " +
                         std::to_string(audio_streams) + ", expected " +
                         std::to_string(contract.audio_streams);
    } else if (contract.frame_count > 0 &&
               std::abs(result.frame_count - contract.frame_count) > 1) {
        contract_ok = false;
        result.failure = "frame count mismatch: got " +
                         std::to_string(result.frame_count) + ", expected " +
                         std::to_string(contract.frame_count);
    } else if (!contract.expected_sha256.empty() &&
               result.sha256 != contract.expected_sha256) {
        contract_ok = false;
        result.failure = "SHA-256 mismatch: got " + result.sha256 +
                         ", expected " + contract.expected_sha256;
    }

    if (!contract_ok) {
        return result;  // passed=true, copy_eligible=false, sha256 populated
    }

    result.copy_eligible = true;
    return result;
}

} // namespace chronon3d::media::video
