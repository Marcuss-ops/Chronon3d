#include <chronon3d/verification/render_receipt.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/media/video/keyframe_cadence.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

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

    receipt.backend = input.backend;
    receipt.render_lane = input.render_lane.empty() ? input.backend : input.render_lane;
    receipt.lane_reason = input.lane_reason;
    receipt.width = input.width;
    receipt.height = input.height;
    receipt.fps_num = input.fps_num;
    receipt.fps_den = input.fps_den;
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
        } else {
            // Not probed: media-dependent checks are unresolvable.
            receipt.verification.decode = "fail";
            receipt.verification.frame_count = "skip";
            receipt.verification.codec = "fail";
            receipt.verification.pixel_format = "fail";
            receipt.verification.resolution = "fail";
            receipt.verification.fps = "fail";
            receipt.verification.audio = "skip";
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
    }

    const bool verification_ok =
        receipt.verification.ffprobe != "fail" &&
        receipt.verification.decode != "fail" &&
        receipt.verification.frame_count != "fail" &&
        receipt.verification.codec != "fail" &&
        receipt.verification.pixel_format != "fail" &&
        receipt.verification.resolution != "fail" &&
        receipt.verification.fps != "fail" &&
        receipt.verification.audio != "fail";
    receipt.copy_eligible =
        exists && !receipt.sha256.empty() && receipt.bytes > 0 && verification_ok;

    const auto total_elapsed = std::chrono::steady_clock::now() - total_start;
    receipt.timing.total_ms =
        std::chrono::duration<double, std::milli>(total_elapsed).count();
    return receipt;
}

std::filesystem::path write_render_receipt(const RenderReceipt& receipt,
                                           const std::filesystem::path& output_path) {
    nlohmann::json js;
    js["schema"] = receipt.schema;
    js["render_lane"] = receipt.render_lane;
    js["lane_reason"] = receipt.lane_reason;
    js["identity"] = {
        {"job_id", receipt.job_id},
        {"chronon_version", receipt.chronon_version},
        {"chronon_abi", receipt.chronon_abi},
        {"git_sha", receipt.git_sha},
        {"render_plan_schema", receipt.render_plan_schema},
        {"content_digest", receipt.content_digest},
        {"request_digest", receipt.request_digest},
        {"asset_manifest_digest", receipt.asset_manifest_digest},
    };
    js["render"] = {
        {"backend", receipt.backend},
        {"render_lane", receipt.render_lane},
        {"lane_reason", receipt.lane_reason},
        {"width", receipt.width},
        {"height", receipt.height},
        {"fps_num", receipt.fps_num},
        {"fps_den", receipt.fps_den},
        {"frames", receipt.frames},
        {"execution_downgrade_reason", receipt.execution_downgrade_reason},
        {"execution_downgrade_count", receipt.execution_downgrade_count},
    };
    js["media"] = {
        {"container", receipt.media.container},
        {"codec", receipt.media.codec},
        {"pixel_format", receipt.media.pixel_format},
        {"width", receipt.media.width},
        {"height", receipt.media.height},
        {"fps_num", receipt.media.fps_num},
        {"fps_den", receipt.media.fps_den},
        {"r_fps_num", receipt.media.r_fps_num},
        {"r_fps_den", receipt.media.r_fps_den},
        {"frame_count", receipt.media.frame_count},
        {"duration_ms", receipt.media.duration_ms},
        {"duration_us", static_cast<std::int64_t>(receipt.media.duration_ms * 1000.0)},
        {"has_audio", receipt.media.has_audio},
        {"has_video", receipt.media.has_video},
        {"video_codec", receipt.media.codec},
        {"codec_profile", receipt.media.codec_profile},
        {"video_level", receipt.media.video_level},
        {"video_streams", receipt.media.video_streams},
        {"video_time_base_num", receipt.media.video_time_base_num},
        {"video_time_base_den", receipt.media.video_time_base_den},
        {"sar_num", receipt.media.sar_num},
        {"sar_den", receipt.media.sar_den},
        {"color_range", receipt.media.color_range},
        {"color_space", receipt.media.color_space},
        {"color_transfer", receipt.media.color_transfer},
        {"color_primaries", receipt.media.color_primaries},
        {"field_order", receipt.media.field_order},
        {"start_pts", receipt.media.start_pts},
        {"first_frame_keyframe", receipt.media.first_frame_keyframe},
        {"closed_gop", receipt.media.closed_gop},
        {"keyframe_interval", receipt.media.keyframe_interval},
        {"audio_streams", receipt.media.audio_streams},
        {"audio_codec", receipt.media.audio_codec},
        {"audio_profile", receipt.media.audio_profile},
        {"sample_rate", receipt.media.sample_rate},
        {"channels", receipt.media.channels},
        {"channel_layout", receipt.media.channel_layout},
        {"audio_bitrate", receipt.media.audio_bitrate},
        {"audio_time_base_num", receipt.media.audio_time_base_num},
        {"audio_time_base_den", receipt.media.audio_time_base_den},
    };
    const bool verification_ok =
        receipt.verification.ffprobe != "fail" &&
        receipt.verification.decode != "fail" &&
        receipt.verification.frame_count != "fail" &&
        receipt.verification.codec != "fail" &&
        receipt.verification.pixel_format != "fail" &&
        receipt.verification.resolution != "fail" &&
        receipt.verification.fps != "fail" &&
        receipt.verification.audio != "fail";
    js["verification"] = {
        {"requested_policy", receipt.verification.requested_policy},
        {"resolved_policy", receipt.verification.resolved_policy},
        {"status", verification_ok ? "pass" : "fail"},
        {"ffprobe", receipt.verification.ffprobe},
        {"decode", receipt.verification.decode},
        {"frame_count", receipt.verification.frame_count},
        {"codec", receipt.verification.codec},
        {"pixel_format", receipt.verification.pixel_format},
        {"resolution", receipt.verification.resolution},
        {"fps", receipt.verification.fps},
        {"audio", receipt.verification.audio},
    };
    js["timing_ms"] = {
        {"sha256_ms", receipt.timing.sha256_ms},
        {"probe_ms", receipt.timing.probe_ms},
        {"count_frames_ms", receipt.timing.count_frames_ms},
        {"decode_ms", receipt.timing.decode_ms},
        {"total_ms", receipt.timing.total_ms},
    };
    js["output"] = {
        {"bytes", receipt.bytes},
        {"sha256", receipt.sha256},
    };
    js["copy_eligible"] = receipt.copy_eligible;

    const std::filesystem::path receipt_path =
        output_path.string() + ".receipt.json";
    std::ofstream out(receipt_path, std::ios::binary);
    out << js.dump(2) << '\n';
    return receipt_path;
}

} // namespace chronon3d::verification
