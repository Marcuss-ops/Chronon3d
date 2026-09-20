#include "daemon_service_job_admission.hpp"

namespace chronon3d::cli {

RenderPolicyDecision decide_render_job_admission(
    bool parallel_disjoint,
    std::uint64_t estimated_vram_bytes) {
    return decide_render_admission(RenderPolicyInput{
        .job_class = parallel_disjoint
            ? RenderJobClass::ChunkedDisjointSamePlan
            : kDefaultRenderJobClass,
        .job_peak_vram_bytes = estimated_vram_bytes,
        .device_total_vram_bytes = kReferenceDeviceVramBytes,
        .device_free_vram_bytes = kReferenceDeviceVramBytes,
    });
}

} // namespace chronon3d::cli
