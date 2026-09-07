#include <chronon3d/runtime/job/render_job_request.hpp>

#include <utility>

namespace chronon3d::runtime::job {
namespace {

bool valid_backend(const std::string& value) {
    return value == "auto" || value == "software" || value == "vulkan";
}

} // namespace

Result<ResolvedRenderJob, RenderJobRequestError>
normalize_render_job(
    const RenderJobRequest& request,
    const RuntimeCapabilities& capabilities) {
    if (request.plan_path.empty()) {
        return RenderJobRequestError{
            RenderJobRequestErrorCode::MissingPlan,
            "render job requires a plan_path"};
    }
    if (!valid_backend(request.backend) || !valid_backend(capabilities.backend)) {
        return RenderJobRequestError{
            RenderJobRequestErrorCode::InvalidBackend,
            "render job backend must be auto, software, or vulkan"};
    }
    if (capabilities.backend != "auto" && request.backend != "auto" &&
        request.backend != capabilities.backend) {
        return RenderJobRequestError{
            RenderJobRequestErrorCode::InvalidBackend,
            "render job backend does not match the host backend"};
    }
    if (request.frame_range &&
        (request.frame_range->first < 0 ||
         request.frame_range->last < 0 ||
         request.frame_range->last < request.frame_range->first)) {
        return RenderJobRequestError{
            RenderJobRequestErrorCode::InvalidFrameRange,
            "render job frame range is invalid"};
    }

    ResolvedRenderJob resolved;
    resolved.plan_path = request.plan_path;
    resolved.output_path = request.output_path;
    resolved.assets_root = request.assets_root;
    resolved.frame_range = request.frame_range;
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
    return resolved;
}

} // namespace chronon3d::runtime::job
