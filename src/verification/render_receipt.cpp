#include <chronon3d/verification/render_receipt.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

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
bool probe_media(const std::filesystem::path& output, MediaContract& media) {
    const std::string command =
        "ffprobe -v error -count_frames -show_streams -show_format -of json '" +
        output.string() + "' 2>/dev/null";
    const std::string raw = run_capture(command);
    if (raw.empty()) return false;

    nlohmann::json js;
    try {
        js = nlohmann::json::parse(raw);
    } catch (...) {
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
                continue;
            }
            if (codec_type != "video") continue;
            media.codec = stream.value("codec_name", "");
            media.pixel_format = stream.value("pix_fmt", "");
            media.width = stream.value("width", 0);
            media.height = stream.value("height", 0);
            parse_rate(stream.value("avg_frame_rate", "0/1"),
                       media.fps_num, media.fps_den);
            media.frame_count = stream_frame_count(stream);
            found_video = true;
        }
    }
    media.probed = found_video;
    return found_video;
}

} // namespace

RenderReceipt build_render_receipt(const RenderReceiptInput& input,
                                   const std::filesystem::path& output_path,
                                   bool is_video) {
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
    receipt.width = input.width;
    receipt.height = input.height;
    receipt.fps_num = input.fps_num;
    receipt.fps_den = input.fps_den;
    receipt.frames = input.frames;

    std::error_code ec;
    const bool exists = std::filesystem::exists(output_path, ec) && !ec &&
        std::filesystem::is_regular_file(output_path, ec) && !ec;
    if (exists) {
        receipt.bytes = static_cast<std::int64_t>(
            std::filesystem::file_size(output_path, ec));
        if (ec) receipt.bytes = -1;
        if (const auto digest = chronon3d::assets::sha256_file(output_path)) {
            receipt.sha256 = digest->hex();
        }
    }

    if (is_video) {
        const bool probed = probe_media(output_path, receipt.media);
        receipt.verification.ffprobe = probed ? "pass" : "fail";

        if (probed) {
            // Full-decode smoke: ffmpeg decodes the entire file to null.
            const std::string decode_cmd =
                "ffmpeg -v error -i '" + output_path.string() +
                "' -f null - > /dev/null 2>&1";
            receipt.verification.decode =
                command_succeeds(decode_cmd) ? "pass" : "fail";

            // Frame count (skip when the container reported none).
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
            const bool fps_ok =
                receipt.media.fps_num == receipt.fps_num &&
                receipt.media.fps_den == receipt.fps_den;
            receipt.verification.fps = fps_ok ? "pass" : "fail";

            // Audio policy: an output with audio tracks requires an audio
            // stream; without audio tracks there is no audio contract (skip).
            receipt.verification.audio = input.has_audio_tracks
                ? (receipt.media.has_audio ? "pass" : "fail")
                : "skip";
        } else {
            // Not probed: media-dependent checks are unresolvable.
            receipt.verification.decode = "skip";
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

    return receipt;
}

std::filesystem::path write_render_receipt(const RenderReceipt& receipt,
                                           const std::filesystem::path& output_path) {
    nlohmann::json js;
    js["schema"] = receipt.schema;
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
        {"width", receipt.width},
        {"height", receipt.height},
        {"fps_num", receipt.fps_num},
        {"fps_den", receipt.fps_den},
        {"frames", receipt.frames},
    };
    js["media"] = {
        {"container", receipt.media.container},
        {"codec", receipt.media.codec},
        {"pixel_format", receipt.media.pixel_format},
        {"width", receipt.media.width},
        {"height", receipt.media.height},
        {"fps_num", receipt.media.fps_num},
        {"fps_den", receipt.media.fps_den},
        {"frame_count", receipt.media.frame_count},
        {"duration_ms", receipt.media.duration_ms},
        {"has_audio", receipt.media.has_audio},
    };
    js["verification"] = {
        {"ffprobe", receipt.verification.ffprobe},
        {"decode", receipt.verification.decode},
        {"frame_count", receipt.verification.frame_count},
        {"codec", receipt.verification.codec},
        {"pixel_format", receipt.verification.pixel_format},
        {"resolution", receipt.verification.resolution},
        {"fps", receipt.verification.fps},
        {"audio", receipt.verification.audio},
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
