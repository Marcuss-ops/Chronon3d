#include <chronon3d/verification/render_receipt.hpp>
#include "render_receipt_probe_internal.hpp"

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

std::FILE* open_process_pipe(const char* command) {
#ifdef _WIN32
    return _popen(command, "r");
#else
    return popen(command, "r");
#endif
}

void close_process_pipe(std::FILE* pipe) {
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
}

/// Run a shell command and capture its stdout.  Empty on failure to spawn.
std::string run_capture(const std::string& command) {
    std::array<char, 4096> buffer{};
    std::string output;
    std::FILE* pipe = open_process_pipe(command.c_str());
    if (!pipe) return output;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    close_process_pipe(pipe);
    return output;
}

/// Exit-code probe (0 on success).
bool command_succeeds(const std::string& command) {
    return std::system(command.c_str()) == 0;
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
        // Fix 2 (TICKET-OVERLAY-THROUGHPUT): never re-hash bytes the caller
        // already digested — one SHA-256 per artifact. The precomputed value
        // is accepted only for an existing regular file (the size above is
        // still read here, so a stale caller digest cannot hide a missing
        // output).
        if (input.precomputed_sha256.has_value()) {
            receipt.sha256 = *input.precomputed_sha256;
            if (input.precomputed_sha256_ms.has_value())
                receipt.timing.sha256_ms = *input.precomputed_sha256_ms;
        } else {
            const auto sha_start = std::chrono::steady_clock::now();
            if (const auto digest = chronon3d::assets::sha256_file(output_path)) {
                receipt.sha256 = digest->hex();
            }
            const auto sha_elapsed = std::chrono::steady_clock::now() - sha_start;
            receipt.timing.sha256_ms =
                std::chrono::duration<double, std::milli>(sha_elapsed).count();
        }
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
            receipt.timing.sha256_ms.value_or(0.0)) +
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - hash_start).count();
    }

    if (is_video) {
        // Fix 2 (TICKET-OVERLAY-THROUGHPUT): the caller may hand in the
        // MediaContract it already observed on the freshly muxed output
        // (canonical in-process probe at the export boundary), so the receipt
        // performs no second probe. HDR outputs are excluded: the mastering
        // comparison below needs the side data only the ffprobe JSON exposes.
        bool probed = false;
        if (input.precomputed_media.has_value() &&
            !input.hdr_mastering_metadata.has_value()) {
            receipt.media = *input.precomputed_media;
            receipt.media.probed = true;
            receipt.timing.ffprobe_ms = input.precomputed_probe_ms;
            probed = true;
        } else {
            double ffprobe_ms = 0.0;
            probed = receipt_probe::probe_media(
                output_path, receipt.media,
                /*require_exit_ok=*/level != VerifyLevel::Certify,
                &ffprobe_ms);
            // The probe attempt ran either way; its cost is measured.
            receipt.timing.ffprobe_ms = ffprobe_ms;
        }
        receipt.verification.ffprobe = probed ? "pass" : "fail";

        if (probed) {
            // Certify-level authoritative frame count: one dedicated
            // `-count_frames` decode pass, timed separately from the
            // metadata probe above so the receipt can attribute the cost.
            if (level == VerifyLevel::Certify) {
                double count_frames_ms = 0.0;
                const auto counted =
                    receipt_probe::count_frames_ffprobe(output_path, &count_frames_ms);
                // The pass ran; its cost is measured.
                receipt.timing.count_frames_ms = count_frames_ms;
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
                ? (receipt_probe::mastering_metadata_matches(*input.hdr_mastering_metadata,
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
