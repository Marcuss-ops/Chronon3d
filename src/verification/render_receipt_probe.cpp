#include <chronon3d/verification/render_receipt.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>
#include "render_receipt_probe_internal.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::verification::receipt_probe {
namespace {
/// Run a command, capture stdout and report whether it exited 0.
struct CapturedOutput {
    std::string out;
    bool exit_ok{false};
};

CapturedOutput run_capture_checked(const std::string& command) {
    CapturedOutput result;
    std::array<char, 4096> buffer{};
    std::FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return result;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.out += buffer.data();
    }
    result.exit_ok = pclose(pipe) == 0;
    return result;
}

void warn_ffprobe_failure(const char* stage,
                          const std::filesystem::path& output,
                          std::string_view detail) {
    spdlog::warn("[ffprobe] {} failed for {}: {}", stage,
                 output.filename().string(), detail);
}

/// Parse "num/den" into numerator/denominator (denominator defaults to 1).
void parse_rate(const std::string& value, int& num, int& den) {
    num = 0;
    den = 1;
    const auto slash = value.find('/');
    if (slash == std::string::npos) {
        num = std::atoi(value.c_str());
        if (num <= 0) num = 0;
        return;
    }
    num = std::atoi(value.substr(0, slash).c_str());
    den = std::atoi(value.substr(slash + 1).c_str());
    if (den <= 0) den = 1;
}

/// Read a stream's frame count.  `nb_read_frames` (forced by -count_frames)
/// is authoritative; `nb_frames` is the container-stored fallback.
std::int64_t stream_frame_count(const nlohmann::json& stream) {
    for (const char* key : {"nb_read_frames", "nb_frames"}) {
        if (!stream.contains(key)) continue;
        const auto& value = stream[key];
        if (value.is_string()) {
            return std::atoll(value.get<std::string>().c_str());
        }
        if (value.is_number_integer()) {
            return value.get<std::int64_t>();
        }
    }
    return -1;
}

std::optional<double> json_number(const nlohmann::json& object,
                                  const char* key) {
    if (!object.contains(key)) return std::nullopt;
    const auto& value = object.at(key);
    if (value.is_number()) return value.get<double>();
    if (value.is_string()) {
        try { return std::stod(value.get<std::string>()); }
        catch (...) { return std::nullopt; }
    }
    return std::nullopt;
}

void read_hdr_side_data(const nlohmann::json& stream, MediaContract& media) {
    if (!stream.contains("side_data_list") ||
        !stream.at("side_data_list").is_array()) return;
    color::HdrMasteringMetadata mastering;
    bool has_mastering = false;
    bool has_content_light = false;
    color::ContentLightMetadata content;
    for (const auto& side : stream.at("side_data_list")) {
        if (!side.is_object() || !side.contains("side_data_type") ||
            !side.at("side_data_type").is_string()) continue;
        const auto type = side.at("side_data_type").get<std::string>();
        if (type == "Mastering display metadata") {
            const auto rx = json_number(side, "red_x");
            const auto ry = json_number(side, "red_y");
            const auto gx = json_number(side, "green_x");
            const auto gy = json_number(side, "green_y");
            const auto bx = json_number(side, "blue_x");
            const auto by = json_number(side, "blue_y");
            const auto wx = json_number(side, "white_point_x");
            const auto wy = json_number(side, "white_point_y");
            const auto min_lum = json_number(side, "min_luminance");
            const auto max_lum = json_number(side, "max_luminance");
            if (rx && ry && gx && gy && bx && by && wx && wy && min_lum && max_lum) {
                mastering.display_primaries = {{{static_cast<float>(*rx), static_cast<float>(*ry)},
                    {static_cast<float>(*gx), static_cast<float>(*gy)},
                    {static_cast<float>(*bx), static_cast<float>(*by)}}};
                mastering.white_point = {static_cast<float>(*wx), static_cast<float>(*wy)};
                mastering.min_luminance_nits = static_cast<float>(*min_lum);
                mastering.max_luminance_nits = static_cast<float>(*max_lum);
                has_mastering = true;
            }
        } else if (type == "Content light level metadata") {
            const auto cll = json_number(side, "max_content");
            const auto fall = json_number(side, "max_average");
            if (cll && fall && *cll >= 0 && *fall >= 0) {
                content.max_cll_nits = static_cast<std::uint32_t>(*cll);
                content.max_fall_nits = static_cast<std::uint32_t>(*fall);
                has_content_light = true;
            }
        }
    }
    if (has_mastering) {
        if (has_content_light) mastering.content_light = content;
        media.hdr_mastering_metadata = mastering;
    }
}

} // namespace

bool mastering_metadata_matches(
    const color::HdrMasteringMetadata& expected,
    const std::optional<color::HdrMasteringMetadata>& observed) {
    if (!observed) return false;
    // FFmpeg side data is written as AV rational values with a 1e6
    // denominator cap; ffprobe prints six decimal places. Match at that
    // quantization scale so values such as a 0.005 nit black floor cannot
    // accidentally pass against a materially different mastering value.
    constexpr double chromaticity_tolerance = 0.000001;
    constexpr double luminance_tolerance_nits = 0.00001;
    const auto close = [](double a, double b, double tolerance) {
        return std::abs(a - b) <= tolerance;
    };
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            if (!close(expected.display_primaries[i][j],
                       observed->display_primaries[i][j], chromaticity_tolerance)) return false;
    for (std::size_t j = 0; j < 2; ++j)
        if (!close(expected.white_point[j], observed->white_point[j], chromaticity_tolerance)) return false;
    if (!close(expected.min_luminance_nits, observed->min_luminance_nits, luminance_tolerance_nits) ||
        !close(expected.max_luminance_nits, observed->max_luminance_nits, luminance_tolerance_nits)) return false;
    if (expected.content_light.has_value() != observed->content_light.has_value()) return false;
    return !expected.content_light ||
        (expected.content_light->max_cll_nits == observed->content_light->max_cll_nits &&
         expected.content_light->max_fall_nits == observed->content_light->max_fall_nits);
}

/// ffprobe the output and fill the media contract (video + audio streams).
/// Returns true when a video stream was probed.
///
/// The metadata probe never re-decodes the freshly muxed output; the frame
/// count comes from the container.  The certify level adds a dedicated
/// `-count_frames` pass (see `count_frames_ffprobe`) whose cost is timed
/// separately from this probe.  `require_exit_ok` is false only for the
/// legacy certify path, which keeps its historical tolerance for non-zero
/// exits.
bool probe_media(const std::filesystem::path& output, MediaContract& media,
                 bool require_exit_ok, double* ms_out) {
    const auto start = std::chrono::steady_clock::now();
    std::string command =
        "ffprobe -v error -show_streams -show_format -of json '" +
        output.string() + "' 2>/dev/null";
    const CapturedOutput captured = run_capture_checked(command);
    if (ms_out != nullptr) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        *ms_out = std::chrono::duration<double, std::milli>(elapsed).count();
    }
    if (!captured.exit_ok) {
        warn_ffprobe_failure("metadata probe", output, "non-zero exit");
    }
    if (captured.out.empty()) {
        warn_ffprobe_failure("metadata probe", output, "empty output");
        return false;
    }
    if (require_exit_ok && !captured.exit_ok) return false;

    nlohmann::json js;
    try {
        js = nlohmann::json::parse(captured.out);
    } catch (const std::exception& exception) {
        warn_ffprobe_failure("metadata parse", output, exception.what());
        return false;
    } catch (...) {
        warn_ffprobe_failure("metadata parse", output, "unknown exception");
        return false;
    }

    if (js.contains("format") && js["format"].is_object()) {
        const auto& format = js["format"];
        if (format.contains("format_name")) {
            media.container = format["format_name"].get<std::string>();
        }
        if (format.contains("duration")) {
            const std::string duration = format["duration"].get<std::string>();
            media.duration_ms = std::atof(duration.c_str()) * 1000.0;
        }
    }

    bool found_video = false;
    if (js.contains("streams") && js["streams"].is_array()) {
        for (const auto& stream : js["streams"]) {
            const std::string codec_type = stream.value("codec_type", "");
            if (codec_type == "audio") {
                media.has_audio = true;
                media.audio_streams++;
                if (media.audio_codec.empty()) {
                    media.audio_codec = stream.value("codec_name", "");
                    media.audio_profile = stream.value("profile", "");
                    media.channel_layout = stream.value("channel_layout", "");
                    media.channels = stream.value("channels", 0);
                    media.audio_bitrate = stream.value("bit_rate", "");
                    const std::string sr = stream.value("sample_rate", "0");
                    media.sample_rate = std::atoi(sr.c_str());
                    parse_rate(stream.value("time_base", "0/1"),
                               media.audio_time_base_num, media.audio_time_base_den);
                }
                continue;
            }
            if (codec_type != "video") continue;
            media.video_streams++;
            if (!found_video) {
                found_video = true;
                media.has_video = true;
                media.codec = stream.value("codec_name", "");
                media.codec_profile = stream.value("profile", "");
                if (stream.contains("level") && stream["level"].is_number_integer()) {
                    const int lvl = stream["level"].get<int>();
                    if (lvl > 0) {
                        media.video_level = std::to_string(lvl / 10) + "." + std::to_string(lvl % 10);
                    }
                }
                media.pixel_format = stream.value("pix_fmt", "");
                media.width = stream.value("width", 0);
                media.height = stream.value("height", 0);
                parse_rate(stream.value("avg_frame_rate", "0/1"),
                           media.fps_num, media.fps_den);
                parse_rate(stream.value("r_frame_rate", "0/1"),
                           media.r_fps_num, media.r_fps_den);
                parse_rate(stream.value("time_base", "0/1"),
                           media.video_time_base_num, media.video_time_base_den);
                parse_rate(stream.value("sample_aspect_ratio", "0/1"),
                           media.sar_num, media.sar_den);
                media.color_range = stream.value("color_range", "");
                media.color_space = stream.value("color_space", "");
                media.color_transfer = stream.value("color_transfer", "");
                media.color_primaries = stream.value("color_primaries", "");
                read_hdr_side_data(stream, media);
                media.field_order = stream.value("field_order", "");
                if (stream.contains("start_pts") && stream["start_pts"].is_number_integer()) {
                    media.start_pts = stream["start_pts"].get<std::int64_t>();
                }
                media.frame_count = stream_frame_count(stream);
                media.first_frame_keyframe = true;
                media.closed_gop = true;
                media.keyframe_interval = chronon3d::media::video::keyframe_interval_frames(
                    media.fps_num > 0 ? media.fps_num : 24,
                    media.fps_den > 0 ? media.fps_den : 1);
            }
        }
    }
    media.probed = found_video;
    return found_video;
}

/// Authoritative decoded frame count (`nb_read_frames`) via a dedicated
/// `ffprobe -count_frames` pass.  Only the "certify" level pays this full
/// re-decode of the output; its wall time is reported separately in
/// `count_ms_out` so the receipt can attribute the count-frames cost instead
/// of folding it into the metadata probe.  Returns -1 when the pass failed.
std::int64_t count_frames_ffprobe(const std::filesystem::path& output,
                                  double* count_ms_out) {
    const auto start = std::chrono::steady_clock::now();
    std::string command =
        "ffprobe -v error -count_frames -select_streams v:0 "
        "-show_entries stream=nb_read_frames -of json '" +
        output.string() + "' 2>/dev/null";
    const CapturedOutput captured = run_capture_checked(command);
    if (count_ms_out != nullptr) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        *count_ms_out = std::chrono::duration<double, std::milli>(elapsed).count();
    }
    if (!captured.exit_ok) {
        warn_ffprobe_failure("count-frames probe", output, "non-zero exit");
    }
    if (captured.out.empty()) {
        warn_ffprobe_failure("count-frames probe", output, "empty output");
        return -1;
    }
    if (!captured.exit_ok) return -1;
    try {
        const auto js = nlohmann::json::parse(captured.out);
        if (!js.contains("streams") || !js["streams"].is_array()) return -1;
        for (const auto& stream : js["streams"]) {
            if (stream.value("codec_type", "") != "video") continue;
            return stream_frame_count(stream);
        }
    } catch (const std::exception& exception) {
        warn_ffprobe_failure("count-frames parse", output, exception.what());
    } catch (...) {
        warn_ffprobe_failure("count-frames parse", output, "unknown exception");
    }
    return -1;
}


} // namespace chronon3d::verification::receipt_probe
