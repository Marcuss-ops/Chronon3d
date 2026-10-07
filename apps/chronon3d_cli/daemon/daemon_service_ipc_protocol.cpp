#include "daemon_service_ipc_protocol.hpp"
#include "daemon_render_job_payload.hpp"
#include "daemon_service.hpp"

#include <chronon3d/media/video/video_execution_resolver.hpp>
#include <chronon3d/media/video/packet_assembler.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::cli {

namespace {

std::string spec_or_root(const nlohmann::json& payload, const char* key) {
    const auto it = payload.find("output_spec");
    if (it != payload.end() && it->is_object() && it->contains(key) &&
        !(*it)[key].is_null()) {
        return (*it)[key].get<std::string>();
    }
    const auto root = payload.find(key);
    if (root != payload.end() && !root->is_null()) {
        return root->get<std::string>();
    }
    return {};
}

std::int64_t spec_or_root_int(
    const nlohmann::json& payload, const char* key, std::int64_t fallback) {
    const auto it = payload.find("output_spec");
    if (it != payload.end() && it->is_object() && it->contains(key) &&
        !(*it)[key].is_null()) {
        return (*it)[key].get<std::int64_t>();
    }
    const auto root = payload.find(key);
    if (root != payload.end() && !root->is_null()) {
        return root->get<std::int64_t>();
    }
    return fallback;
}

constexpr std::string_view kRenderJobRootKeys[] = {
    "job_id", "plan_path", "prepared_package_path", "output", "assets_root", "report",
    "trace_output", "trace_level", "receipt_verify",
    "range_enabled", "first_frame", "last_frame", "parallel_disjoint",
    "codec", "hardware_encoder", "hardware", "encoder_backend",
    "ffmpeg_mode", "gpu_hot_path_mode", "encode_preset", "rate_control_mode",
    "crf", "qp", "bitrate", "audio_source_path",
    "audio_target_sample_rate", "gop_copy_only", "changed_intervals",
    "pipe_pixfmt", "fps_num", "fps_den", "backend",
    "execution_requirements", "output_spec",
};

constexpr std::string_view kRenderJobOutputSpecKeys[] = {
    "codec", "width", "height", "fps_num", "fps_den", "pipe_pixfmt",
    "hardware_encoder", "encoder_backend", "gpu_hot_path_mode",
    "encode_preset", "rate_control_mode", "crf", "qp", "bitrate",
    "audio_target_sample_rate", "changed_intervals", "ffmpeg_mode",
};

constexpr std::string_view kRenderJobRequirementKeys[] = {
    "backend", "gpu_required", "cpu_fallback_allowed",
    "composition_required", "video_source_required", "packet_copy_allowed",
};

constexpr std::string_view kChangedIntervalKeys[] = {
    "start_seconds", "end_seconds", "kind",
};

template <std::size_t N>
[[nodiscard]] bool payload_key_known(
    std::string_view key, const std::string_view (&keys)[N]) noexcept {
    for (const auto candidate : keys) {
        if (key == candidate) return true;
    }
    return false;
}

void refuse_unknown_key(std::vector<std::string>& unknown,
                        std::string_view scope, std::string_view key) {
    unknown.push_back(std::string{scope} + "." + std::string{key});
}

void reject_unknown_render_job_keys(const nlohmann::json& request) {
    if (!request.is_object()) {
        throw std::runtime_error("RENDER_JOB payload must be a JSON object");
    }
    std::vector<std::string> unknown;
    for (const auto& item : request.items()) {
        if (!payload_key_known(item.key(), kRenderJobRootKeys)) {
            refuse_unknown_key(unknown, "payload", item.key());
        }
    }
    if (const auto spec = request.find("output_spec"); spec != request.end()) {
        if (!spec->is_object()) {
            throw std::runtime_error(
                "RENDER_JOB output_spec must be a JSON object");
        }
        for (const auto& item : spec->items()) {
            if (!payload_key_known(item.key(), kRenderJobOutputSpecKeys)) {
                refuse_unknown_key(unknown, "output_spec", item.key());
            }
        }
    }
    if (const auto reqs = request.find("execution_requirements");
        reqs != request.end()) {
        if (!reqs->is_object()) {
            throw std::runtime_error(
                "RENDER_JOB execution_requirements must be a JSON object");
        }
        for (const auto& item : reqs->items()) {
            if (!payload_key_known(item.key(), kRenderJobRequirementKeys)) {
                refuse_unknown_key(unknown, "execution_requirements", item.key());
            }
        }
    }
    const auto reject_interval_entry_keys =
        [&unknown](const nlohmann::json& container, std::string_view scope) {
            const auto intervals = container.find("changed_intervals");
            if (intervals == container.end() || !intervals->is_array()) return;
            for (const auto& entry : *intervals) {
                if (!entry.is_object()) continue;
                for (const auto& item : entry.items()) {
                    if (!payload_key_known(item.key(), kChangedIntervalKeys)) {
                        refuse_unknown_key(unknown, scope, item.key());
                    }
                }
            }
        };
    reject_interval_entry_keys(request, "changed_intervals[]");
    if (const auto spec = request.find("output_spec");
        spec != request.end() && spec->is_object()) {
        reject_interval_entry_keys(*spec, "output_spec.changed_intervals[]");
    }
    if (unknown.empty()) return;

    std::string message = "RENDER_JOB payload contains unknown key(s): ";
    for (std::size_t index = 0; index < unknown.size(); ++index) {
        if (index != 0) message += ", ";
        message += unknown[index];
    }
    message += ". The payload vocabulary is closed: remove the key or spell it "
               "as the RENDER_JOB contract does.";
    throw std::runtime_error(message);
}

} // namespace

PlanDimensions extract_plan_dimensions(
    const std::filesystem::path& plan_path,
    std::uint32_t& width,
    std::uint32_t& height) {
    if (width > 0 && height > 0) return PlanDimensions::NotRequested;
    if (plan_path.empty()) return PlanDimensions::NotRequested;

    std::ifstream file(plan_path);
    if (!file.is_open()) {
        spdlog::error("[daemon] plan dimensions unavailable: cannot open '{}'",
                      plan_path.string());
        return PlanDimensions::Unreadable;
    }
    try {
        nlohmann::json plan_json;
        file >> plan_json;
        if (plan_json.contains("canvas") && plan_json["canvas"].is_object()) {
            width = plan_json["canvas"].value("width", 0U);
            height = plan_json["canvas"].value("height", 0U);
        } else if (plan_json.contains("width") && plan_json.contains("height")) {
            width = plan_json.value("width", 0U);
            height = plan_json.value("height", 0U);
        }
    } catch (const std::exception& exception) {
        spdlog::error("[daemon] plan dimensions unavailable: '{}' is not a readable "
                      "plan ({})", plan_path.string(), exception.what());
        width = 0;
        height = 0;
        return PlanDimensions::Unreadable;
    }
    return (width > 0 && height > 0) ? PlanDimensions::Resolved
                                     : PlanDimensions::NoCanvasField;
}

DecodedRenderJobPayload decode_render_job_payload(
    const std::string& payload) {
    return decode_render_job_payload(nlohmann::json::parse(payload));
}

DecodedRenderJobPayload decode_render_job_payload(
    const nlohmann::json& request) {
    reject_unknown_render_job_keys(request);

    DecodedRenderJobPayload decoded;
    decoded.job_id = request.value("job_id", std::string{});
    if (decoded.job_id.size() > 256) {
        throw std::runtime_error("RENDER_JOB job_id exceeds 256 bytes");
    }
    auto& job_request = decoded.request;
    job_request.plan_path = request.value("plan_path", "");
    job_request.prepared_package_path = request.value("prepared_package_path", "");
    job_request.output_path = request.value("output", "");
    job_request.assets_root = request.value("assets_root", "");
    job_request.report = request.value("report", false);
    job_request.trace.output = request.value("trace_output", "");
    job_request.trace.level = request.value("trace_level", "pipeline");
    job_request.receipt_verification = request.value("receipt_verify", "");

    if (request.value("range_enabled", false)) {
        job_request.frame_range = runtime::job::FrameRange{
            request.value("first_frame", std::int64_t{0}),
            request.value("last_frame", std::int64_t{0})};
    }
    job_request.parallel_disjoint = request.value("parallel_disjoint", false);

    auto& out = job_request.output;
    out.codec = spec_or_root(request, "codec");
    out.hardware_encoder = chronon3d::media::normalize_hardware_encoder_field(
        spec_or_root(request, "hardware_encoder"));
    if (out.hardware_encoder.empty()) {
        out.hardware_encoder = chronon3d::media::normalize_hardware_encoder_field(
            request.value("hardware", ""));
    }
    out.encoder_backend = spec_or_root(request, "encoder_backend");
    out.ffmpeg_mode = spec_or_root(request, "ffmpeg_mode");
    out.gpu_hot_path_mode = spec_or_root(request, "gpu_hot_path_mode");
    out.encode_preset = spec_or_root(request, "encode_preset");
    out.rate_control_mode = spec_or_root(request, "rate_control_mode");
    out.crf = static_cast<int>(spec_or_root_int(request, "crf", -1));
    out.qp = static_cast<int>(spec_or_root_int(request, "qp", -1));
    out.bitrate = spec_or_root_int(request, "bitrate", 0);
    out.audio_source_path = request.value("audio_source_path", "");
    out.audio_target_sample_rate = static_cast<int>(
        spec_or_root_int(request, "audio_target_sample_rate", 0));
    out.gop_copy_only = request.value("gop_copy_only", false);

    const nlohmann::json* intervals = nullptr;
    const auto spec_it = request.find("output_spec");
    if (spec_it != request.end() && spec_it->is_object() &&
        spec_it->contains("changed_intervals")) {
        intervals = &(*spec_it)["changed_intervals"];
    } else if (request.contains("changed_intervals")) {
        intervals = &request["changed_intervals"];
    }
    if (intervals) {
        if (!intervals->is_array()) {
            throw std::runtime_error("changed_intervals must be a JSON array");
        }
        for (const auto& item : *intervals) {
            if (!item.is_object()) {
                throw std::runtime_error(
                    "changed_intervals entries must be objects");
            }
            const auto kind = item.value("kind", std::string{"content"});
            media::ChangedIntervalKind interval_kind;
            if (kind == "content") {
                interval_kind = media::ChangedIntervalKind::Content;
            } else if (kind == "trim") {
                interval_kind = media::ChangedIntervalKind::Trim;
            } else {
                throw std::runtime_error(
                    "changed_intervals kind must be content or trim");
            }
            out.changed_intervals.push_back(media::ChangedInterval{
                item.value("start_seconds", 0.0),
                item.value("end_seconds", 0.0), interval_kind});
        }
    }
    out.pipe_pixfmt = spec_or_root(request, "pipe_pixfmt");
    out.fps_num = static_cast<std::uint32_t>(
        spec_or_root_int(request, "fps_num", 0));
    out.fps_den = static_cast<std::uint32_t>(
        spec_or_root_int(request, "fps_den", 1));

    media::ExecutionRequirements exec_reqs;
    if (request.contains("execution_requirements")) {
        const auto& sr = request["execution_requirements"];
        const std::string requirements_backend = sr.value("backend", "");
        if (job_request.backend == "auto" &&
            !requirements_backend.empty() && requirements_backend != "auto") {
            job_request.backend = requirements_backend;
        }
        exec_reqs.gpu_required = sr.value("gpu_required", false);
        exec_reqs.cpu_fallback_allowed =
            sr.value("cpu_fallback_allowed", true);
        exec_reqs.composition_required =
            sr.value("composition_required", true);
        exec_reqs.video_source_required =
            sr.value("video_source_required", true);
        exec_reqs.packet_copy_allowed =
            sr.value("packet_copy_allowed", true);
    } else {
        exec_reqs.gpu_required =
            chronon3d::media::hardware_encoder_requests_nvenc(out.hardware_encoder);
    }
    job_request.requirements = exec_reqs;
    job_request.backend = request.value("backend", job_request.backend);
    return decoded;
}

ipc::Reply DaemonService::ipc_assemble_segments(const std::string& payload) {
#if defined(CHRONON3D_ENABLE_NATIVE_FFMPEG)
    try {
        const auto request = nlohmann::json::parse(payload.begin(), payload.end());
        const auto paths = request.value("input_paths", std::vector<std::string>{});
        const auto output = request.value("output_path", std::string{});
        const auto result = media::assemble_segments(media::SegmentAssemblyRequest{paths, output});
        if (!result.success) return ipc::Reply{ipc::Status::Error, result.reason};
        return ipc::Reply{ipc::Status::Ok, output};
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::BadRequest,
                          std::string{"ASSEMBLE_SEGMENTS parse failed: "} + e.what()};
    }
#else
    (void)payload;
    return ipc::Reply{ipc::Status::Error,
                      "ASSEMBLE_SEGMENTS unavailable: built without native FFmpeg mux"};
#endif
}

} // namespace chronon3d::cli
