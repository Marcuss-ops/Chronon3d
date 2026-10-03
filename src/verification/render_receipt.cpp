#include <chronon3d/verification/render_receipt.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace chronon3d::verification {

namespace {

/// Run a shell command and capture its stdout.  Empty on failure to spawn.
std::string run_capture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;
    std::FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    pclose(pipe);
    return output;
}

/// Exit-code probe (0 on success).
bool command_succeeds(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

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

std::filesystem::path rendered_image_path(
    const std::filesystem::path& output_pattern,
    std::int64_t frame_index,
    bool is_sequence) {
    std::string path = output_pattern.string();
    std::string frame = std::to_string(frame_index);
    if (frame.size() < 4) frame.insert(frame.begin(), 4 - frame.size(), '0');
    const auto marker = path.find("####");
    if (marker != std::string::npos) {
        path.replace(marker, 4, frame);
        return path;
    }
    if (is_sequence) {
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) path += "_" + frame;
        else path.insert(dot, "_" + frame);
    }
    return path;
}

void warn_ffprobe_failure(const char* stage,
                          const std::filesystem::path& output,
                          std::string_view detail) {
    spdlog::warn("[ffprobe] {} failed for {}: {}", stage,
                 output.filename().string(), detail);
}

/// Post-render verification policy.
///   "fast"    — container/stream metadata only; the freshly-muxed output is
///               NOT re-decoded.  Default: the encoder + muxer just succeeded
///               in-process and no consumer gates on the receipt's decode
///               verdict, so a full re-decode of the whole output would only
///               slow the render lane down.
///   "normal"  — ONE full decode pass (`ffmpeg -f null -`, multithreaded) as
///               the decodability gate; frame count comes from the container.
///   "certify" — historical strict gate: full `ffprobe -count_frames` decode
///               (frame count) PLUS the independent `ffmpeg -f null -` smoke.
enum class VerifyLevel { Fast, Normal, Certify };

/// Resolve the verification level from a raw policy string.  The raw policy
/// is the per-job requested value (daemon RENDER_JOB payload / IPC) or the
/// CHRONON_RECEIPT_VERIFY environment value; "" and unknown values resolve
/// to Fast (the production hot path).
VerifyLevel verify_level_from(const std::string& raw) {
    if (raw == "normal") return VerifyLevel::Normal;
    if (raw == "certify") return VerifyLevel::Certify;
    return VerifyLevel::Fast;
}

/// Raw requested policy: explicit per-job policy first, then the
/// CHRONON_RECEIPT_VERIFY environment ("" when unset).  Recorded on the
/// receipt so consumers can distinguish "no policy requested" from the
/// resolved default.
std::string requested_verify_policy(const std::string& explicit_policy) {
    if (!explicit_policy.empty()) {
        return explicit_policy;
    }
    if (const char* raw = std::getenv("CHRONON_RECEIPT_VERIFY")) {
        return std::string(raw);
    }
    return std::string();
}

/// Canonical name of the policy level that will actually run.
std::string resolved_verify_policy_name(VerifyLevel level) {
    switch (level) {
        case VerifyLevel::Normal: return "normal";
        case VerifyLevel::Certify: return "certify";
        default: return "fast";
    }
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
                media.keyframe_interval = media::video::keyframe_interval_frames(
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

} // namespace

RenderReceipt build_render_receipt(const RenderReceiptInput& input,
                                   const std::filesystem::path& output_path,
                                   bool is_video) {
    const auto total_start = std::chrono::steady_clock::now();
    RenderReceipt receipt;

    // Reuse the caller-provided identity — never recompute it.
    receipt.job_id = input.job_id;
    receipt.chronon_version = input.chronon_version;
    receipt.chronon_abi = input.chronon_abi;
    receipt.git_sha = input.git_sha;
    receipt.render_plan_schema = input.render_plan_schema;
    receipt.content_digest = input.content_digest;
    receipt.request_digest = input.request_digest;
    receipt.asset_manifest_digest = input.asset_manifest_digest;
    receipt.output_color_space = input.output_color_space;
    receipt.reference_white_nits = input.reference_white_nits;
    receipt.ocio_output_display = input.ocio_output_display;
    receipt.ocio_output_view = input.ocio_output_view;
    receipt.hdr_mastering_metadata = input.hdr_mastering_metadata;

    receipt.backend = input.backend;
    receipt.render_lane = input.render_lane.empty() ? input.backend : input.render_lane;
    receipt.lane_reason = input.lane_reason;
    receipt.width = input.width;
    receipt.height = input.height;
    receipt.fps_num = input.fps_num;
    receipt.fps_den = input.fps_den;
    receipt.first_frame = input.first_frame;
    receipt.frames = input.frames;
    receipt.execution_downgrade_reason = input.execution_downgrade_reason;
    receipt.execution_downgrade_count = input.execution_downgrade_count;

    // Record the verification policy that was requested and the level that
    // will actually run, before any output-dependent work.  The explicit
    // per-job policy (when supplied) wins over the environment so a daemon
    // job can never silently drift from the policy the caller requested.
    const std::string policy_raw = requested_verify_policy(
        input.receipt_verify_policy);
    const VerifyLevel level = verify_level_from(policy_raw);
    receipt.verification.requested_policy = policy_raw;
    receipt.verification.resolved_policy = resolved_verify_policy_name(level);

    std::error_code ec;
    const bool exists = std::filesystem::exists(output_path, ec) && !ec &&
        std::filesystem::is_regular_file(output_path, ec) && !ec;
    if (exists) {
        receipt.bytes = static_cast<std::int64_t>(
            std::filesystem::file_size(output_path, ec));
        if (ec) receipt.bytes = -1;
        const auto sha_start = std::chrono::steady_clock::now();
        if (const auto digest = chronon3d::assets::sha256_file(output_path)) {
            receipt.sha256 = digest->hex();
        }
        const auto sha_elapsed = std::chrono::steady_clock::now() - sha_start;
        receipt.timing.sha256_ms =
            std::chrono::duration<double, std::milli>(sha_elapsed).count();
    }

    if (!is_video) {
        receipt.verification.frame_images = "pass";
        const auto hash_start = std::chrono::steady_clock::now();
        if (input.frames <= 0 || input.frames > 1'000'000 || input.first_frame < 0 ||
            input.first_frame > std::numeric_limits<std::int64_t>::max() - input.frames) {
            receipt.verification.frame_images = "fail";
        } else {
            const bool is_sequence = input.frames > 1;
            receipt.frame_images.reserve(static_cast<std::size_t>(input.frames));
            for (std::int64_t offset = 0; offset < input.frames; ++offset) {
                const auto frame_index = input.first_frame + offset;
                const auto image_path = rendered_image_path(
                    output_path, frame_index, is_sequence);
                ReceiptFrameImage frame_image;
                frame_image.frame_index = frame_index;
                std::error_code image_error;
                if (std::filesystem::is_regular_file(image_path, image_error) && !image_error) {
                    frame_image.bytes = static_cast<std::int64_t>(
                        std::filesystem::file_size(image_path, image_error));
                    if (image_error) frame_image.bytes = -1;
                    if (const auto digest = chronon3d::assets::sha256_file(image_path))
                        frame_image.sha256 = digest->hex();
                }
                if (frame_image.bytes <= 0 || frame_image.sha256.empty())
                    receipt.verification.frame_images = "fail";
                receipt.frame_images.push_back(std::move(frame_image));
            }
        }
        receipt.timing.sha256_ms = std::max(0.0,
            receipt.timing.sha256_ms) +
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - hash_start).count();
    }

    if (is_video) {
        const bool probed = probe_media(
            output_path, receipt.media,
            /*require_exit_ok=*/level != VerifyLevel::Certify,
            &receipt.timing.probe_ms);
        receipt.verification.ffprobe = probed ? "pass" : "fail";

        if (probed) {
            // Certify-level authoritative frame count: one dedicated
            // `-count_frames` decode pass, timed separately from the
            // metadata probe above so the receipt can attribute the cost.
            if (level == VerifyLevel::Certify) {
                const auto counted =
                    count_frames_ffprobe(output_path,
                                         &receipt.timing.count_frames_ms);
                if (counted >= 0) receipt.media.frame_count = counted;
            }

            // Decode gate.  Fast never re-decodes the freshly-muxed output
            // (the encoder + muxer just succeeded in-process); normal runs ONE
            // full multithreaded decode pass; certify adds the historical
            // `ffmpeg -f null -` smoke on top of the count_frames pass above.
            const std::string decode_cmd =
                "ffmpeg -v error -i '" + output_path.string() +
                "' -f null - > /dev/null 2>&1";
            switch (level) {
                case VerifyLevel::Fast:
                    receipt.verification.decode = "skip";
                    break;
                case VerifyLevel::Normal: {
                    const auto decode_start = std::chrono::steady_clock::now();
                    receipt.verification.decode =
                        command_succeeds(decode_cmd) ? "pass" : "fail";
                    const auto decode_elapsed =
                        std::chrono::steady_clock::now() - decode_start;
                    receipt.timing.decode_ms =
                        std::chrono::duration<double, std::milli>(
                            decode_elapsed).count();
                    break;
                }
                case VerifyLevel::Certify: {
                    const auto decode_start = std::chrono::steady_clock::now();
                    receipt.verification.decode =
                        command_succeeds(decode_cmd) ? "pass" : "fail";
                    const auto decode_elapsed =
                        std::chrono::steady_clock::now() - decode_start;
                    receipt.timing.decode_ms =
                        std::chrono::duration<double, std::milli>(
                            decode_elapsed).count();
                    break;
                }
            }

            // Frame count (skip when the container reported none).  Under the
            // certify level this is the decoded nb_read_frames count;
            // otherwise it is the container-stored nb_frames of the
            // freshly-muxed mp4.
            if (receipt.media.frame_count >= 0) {
                receipt.verification.frame_count =
                    receipt.media.frame_count == receipt.frames
                        ? "pass" : "fail";
            } else {
                receipt.verification.frame_count = "skip";
            }

            // Codec contract vs the requested codec (auto = any).
            receipt.verification.codec =
                (input.requested_codec == "auto" ||
                 receipt.media.codec == input.requested_codec) ? "pass" : "fail";

            // Pixel format: a video stream must report a non-empty pix_fmt.
            receipt.verification.pixel_format =
                receipt.media.pixel_format.empty() ? "fail" : "pass";

            // Resolution contract vs the requested canvas.
            receipt.verification.resolution =
                (receipt.media.width == receipt.width &&
                 receipt.media.height == receipt.height) ? "pass" : "fail";

            // Frame-rate contract vs the requested fps.
            // FFmpeg may expose a reduced rational (e.g. 4500/149) for a
            // constant-rate 30 fps stream because of container time-base
            // rounding. The avg_frame_rate alone can also overshoot the true
            // CFR rate by a whole frame over the DTS span (e.g. an exact
            // 24 fps stream reported as 19584/815 ~= 24.029), so the verdict
            // passes when EITHER signal matches the requested rate within one
            // milliframe: avg_frame_rate (safe against VFR drift) or
            // r_frame_rate (the CFR nominal rate, reliable when frame
            // durations are constant). A genuinely variable-rate output
            // disagrees with both and is rejected. The exact requested rate
            // stays preserved in `render`.
            const auto fps_of = [](int num, int den) {
                return den > 0 ? static_cast<double>(num) /
                    static_cast<double>(den) : 0.0;
            };
            const double requested_fps = fps_of(receipt.fps_num,
                                                receipt.fps_den);
            const bool fps_ok = requested_fps > 0.0 &&
                (std::abs(fps_of(receipt.media.fps_num,
                                 receipt.media.fps_den) - requested_fps) <= 0.001 ||
                 std::abs(fps_of(receipt.media.r_fps_num,
                                 receipt.media.r_fps_den) - requested_fps) <= 0.001);
            receipt.verification.fps = fps_ok ? "pass" : "fail";

            // Audio policy: an output with audio tracks requires an audio
            // stream; without audio tracks there is no audio contract (skip).
            receipt.verification.audio = input.has_audio_tracks
                ? (receipt.media.has_audio ? "pass" : "fail")
                : "skip";
            receipt.verification.hdr_mastering_metadata = input.hdr_mastering_metadata
                ? (mastering_metadata_matches(*input.hdr_mastering_metadata,
                      receipt.media.hdr_mastering_metadata) ? "pass" : "fail")
                : "skip";
        } else {
            // Not probed: media-dependent checks are unresolvable.
            receipt.verification.decode = "fail";
            receipt.verification.frame_count = "skip";
            receipt.verification.codec = "fail";
            receipt.verification.pixel_format = "fail";
            receipt.verification.resolution = "fail";
            receipt.verification.fps = "fail";
            receipt.verification.audio = "skip";
            receipt.verification.hdr_mastering_metadata =
                input.hdr_mastering_metadata ? "fail" : "skip";
        }
    } else {
        // Still image: media checks do not apply; eligibility is driven by
        // file existence + SHA-256 only.
        receipt.verification.ffprobe = "skip";
        receipt.verification.decode = "skip";
        receipt.verification.frame_count = "skip";
        receipt.verification.codec = "skip";
        receipt.verification.pixel_format = "skip";
        receipt.verification.resolution = "skip";
        receipt.verification.fps = "skip";
        receipt.verification.audio = "skip";
        receipt.verification.hdr_mastering_metadata =
            input.hdr_mastering_metadata ? "fail" : "skip";
    }

    const bool verification_ok =
        receipt.verification.ffprobe != "fail" &&
        receipt.verification.decode != "fail" &&
        receipt.verification.frame_count != "fail" &&
        receipt.verification.codec != "fail" &&
        receipt.verification.pixel_format != "fail" &&
        receipt.verification.resolution != "fail" &&
        receipt.verification.fps != "fail" &&
        receipt.verification.audio != "fail" &&
        receipt.verification.hdr_mastering_metadata != "fail" &&
        receipt.verification.frame_images != "fail";
    receipt.copy_eligible =
        exists && !receipt.sha256.empty() && receipt.bytes > 0 && verification_ok;

    const auto total_elapsed = std::chrono::steady_clock::now() - total_start;
    receipt.timing.total_ms =
        std::chrono::duration<double, std::milli>(total_elapsed).count();
    return receipt;
}

} // namespace chronon3d::verification
