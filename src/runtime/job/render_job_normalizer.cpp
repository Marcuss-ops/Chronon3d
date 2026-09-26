#include <chronon3d/runtime/job/render_job_request.hpp>
#include <chronon3d/runtime/frame_format.hpp>  // canonical pixel-format authority

#include <string>
#include <utility>

namespace chronon3d::runtime::job {
namespace {

bool valid_backend(const std::string& value) {
    return value == "auto" || value == "software" || value == "vulkan";
}

void append_field(std::string& out, const char* key, const std::string& value) {
    out += key;
    out += '=';
    out += value;
    out += '|';
}

void append_flag(std::string& out, const char* key, bool value) {
    out += key;
    out += value ? "=1|" : "=0|";
}

void append_int(std::string& out, const char* key, std::int64_t value) {
    out += key;
    out += '=';
    out += std::to_string(value);
    out += '|';
}

} // namespace

Result<NormalizedRenderJob, RenderJobRequestError>
normalize_render_job(
    const RenderJobRequest& request,
    const RuntimeCapabilities& capabilities) {
    if (request.plan_path.empty()) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::MissingPlan,
            "render job requires a plan_path");
    }
    if (!valid_backend(request.backend) || !valid_backend(capabilities.backend)) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::InvalidBackend,
            "render job backend must be auto, software, or vulkan");
    }
    if (capabilities.backend != "auto" && request.backend != "auto" &&
        request.backend != capabilities.backend) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::InvalidBackend,
            "render job backend does not match the host backend");
    }
    if (request.frame_range &&
        (request.frame_range->first < 0 ||
         request.frame_range->last < 0 ||
         request.frame_range->last < request.frame_range->first)) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::InvalidFrameRange,
            "render job frame range is invalid");
    }
    if (request.parallel_disjoint && !request.frame_range) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::InvalidFrameRange,
            "parallel disjoint admission requires an explicit frame range");
    }
    // Boundary validation for the pipe pixel format. This is the one place a
    // plan-file request and a daemon/IPC request both pass through, and before
    // it existed an unknown name travelled all the way to the runtime parser,
    // which silently mapped it onto RGBA: a formally successful export in the
    // wrong pixel format. The CLI flag layer validates its own input; this
    // closes the programmatic routes against the SAME canonical vocabulary.
    if (!request.output.pipe_pixfmt.empty() &&
        !chronon3d::runtime::try_parse_pixel_format(request.output.pipe_pixfmt)) {
        return make_render_job_request_error(
            RenderJobRequestErrorCode::InvalidPipePixelFormat,
            "render job pipe pixel format '" + request.output.pipe_pixfmt +
                "' is not one of rgba, yuv420p, nv12, p010, p010le");
    }
    for (const auto& interval : request.output.changed_intervals) {
        if (!interval.valid()) {
            return make_render_job_request_error(
                RenderJobRequestErrorCode::InvalidChangedInterval,
                "render job changed interval must be finite, non-negative, and have end_seconds > start_seconds");
        }
    }

    NormalizedRenderJob resolved;
    resolved.plan_path = request.plan_path;
    resolved.prepared_package_path = request.prepared_package_path;
    resolved.output_path = request.output_path;
    resolved.assets_root = request.assets_root;
    resolved.frame_range = request.frame_range;
    resolved.parallel_disjoint = request.parallel_disjoint;
    resolved.requirements = request.requirements;
    resolved.output = request.output;
    resolved.trace = request.trace;
    resolved.receipt_verification = request.receipt_verification;
    resolved.report = request.report;

    const auto& daemon_backend = capabilities.backend;
#if defined(CHRONON3D_ENABLE_VIDEO)
    resolved.execution = media::resolve_canonical_execution_parameters(
        resolved.requirements,
        resolved.output.codec,
        request.backend,
        daemon_backend);
#else
    // Video is optional in matrix builds. Keep the same semantic defaults so
    // request normalization remains available to non-video CLI/SDK clients.
    resolved.execution.backend = request.backend != "auto"
        ? request.backend
        : daemon_backend;
    resolved.execution.codec = resolved.output.codec.empty()
        ? "auto" : resolved.output.codec;
    resolved.execution.hardware_encoder = "none";
    resolved.execution.encoder_backend = "pipe";
    resolved.execution.gpu_hot_path_mode = "auto";
    resolved.execution.cpu_fallback_forbidden = !resolved.requirements.cpu_fallback_allowed;
#endif

    if (request.backend == "auto") resolved.backend = resolved.execution.backend;
    else resolved.backend = request.backend;
    if (resolved.backend.empty()) resolved.backend = "auto";

    if (resolved.output.codec.empty()) {
        resolved.output.codec = resolved.execution.codec;
    }
    if (resolved.output.hardware_encoder.empty()) {
        resolved.output.hardware_encoder = resolved.execution.hardware_encoder;
    }
    if (resolved.output.encoder_backend.empty()) {
        resolved.output.encoder_backend = resolved.execution.encoder_backend;
    }
    if (resolved.output.gpu_hot_path_mode.empty()) {
        resolved.output.gpu_hot_path_mode = resolved.execution.gpu_hot_path_mode;
    }
    // NOTE: `output.sink_type` is deliberately NOT back-filled here. It is a
    // REQUEST, not a resolved parameter: empty means "the sink was never
    // named", and the execution layer selects the video lane only when a sink
    // was explicitly requested (a `.png` output must stay an image job even
    // though the canonical VideoSettings default is "ffmpeg"). Leaving it
    // empty keeps CLI and IPC requests fingerprint-identical.
    return resolved;
}

std::string render_job_fingerprint(const NormalizedRenderJob& job) {
    // Canonical equality key for the CLI↔IPC parity gate. The fingerprint
    // covers exactly what the executor consumes from the normalized job — the
    // boundary/resolution fields that normalize_render_job back-fills from the
    // canonical execution decision (codec/hardware/encoder/hot path) plus the
    // carried request semantics (paths, range, output spec, trace, receipt,
    // report, backend). Raw requirement flags are deliberately NOT part of
    // the key: they are resolution inputs with transport-specific spellings;
    // two transports must agree on the RESOLVED job they execute, which is
    // this fingerprint. Field order is the contract; append new axes at the
    // END so existing fingerprints keep their meaning across extensions.
    std::string fp;
    append_field(fp, "plan", job.plan_path.string());
    append_field(fp, "prepared_package", job.prepared_package_path.string());
    append_field(fp, "output", job.output_path.string());
    append_field(fp, "assets", job.assets_root.string());
    if (job.frame_range) {
        append_int(fp, "range_first", job.frame_range->first);
        append_int(fp, "range_last", job.frame_range->last);
    } else {
        fp += "range=none|";
    }
    append_flag(fp, "parallel_disjoint", job.parallel_disjoint);
    append_field(fp, "backend", job.backend);
    append_flag(fp, "report", job.report);
    const auto& out = job.output;
    append_field(fp, "out_codec", out.codec);
    append_field(fp, "out_hardware", out.hardware_encoder);
    append_field(fp, "out_encoder_backend", out.encoder_backend);
    append_field(fp, "out_ffmpeg_mode", out.ffmpeg_mode);
    append_field(fp, "out_hot_path", out.gpu_hot_path_mode);
    append_field(fp, "out_encode_preset", out.encode_preset);
    append_field(fp, "out_rate_control", out.rate_control_mode);
    append_int(fp, "out_crf", out.crf);
    append_int(fp, "out_qp", out.qp);
    append_int(fp, "out_bitrate", out.bitrate);
    append_field(fp, "out_audio_source", out.audio_source_path);
    append_field(fp, "out_gop_source", out.gop_source);
    append_int(fp, "out_audio_target_sample_rate", out.audio_target_sample_rate);
    append_flag(fp, "out_gop_copy_only", out.gop_copy_only);
    append_int(fp, "out_changed_intervals_count",
               static_cast<std::int64_t>(out.changed_intervals.size()));
    for (const auto& interval : out.changed_intervals) {
        append_field(fp, "out_changed_interval_start",
                     std::to_string(interval.start_seconds));
        append_field(fp, "out_changed_interval_end",
                     std::to_string(interval.end_seconds));
        append_int(fp, "out_changed_interval_kind",
                   static_cast<std::int64_t>(interval.kind));
    }
    append_int(fp, "out_fps_num", out.fps_num);
    append_int(fp, "out_fps_den", out.fps_den);
    append_field(fp, "out_pipe_pixfmt", out.pipe_pixfmt);
    // Pre-encode ("raw") and encoded ("ffmpeg") jobs produce different
    // artifacts from the same render, so the sink is part of the output
    // contract and belongs in the equality key.
    append_field(fp, "out_sink", out.sink_type);
    append_field(fp, "trace_output", job.trace.output.string());
    append_field(fp, "trace_level", job.trace.level);
    append_field(fp, "receipt_verify", job.receipt_verification);
    return fp;
}

} // namespace chronon3d::runtime::job
