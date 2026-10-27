#include "render_plan_compiler_entry_detail.hpp"

namespace chronon3d::render_plan::compiler_entry_detail {

std::optional<PlanDecodeError> validate_compile_request(
    const RenderPlan& plan,
    const RuntimeCameraSampleProvider* runtime_camera) {
    if (runtime_camera != nullptr &&
        (!runtime_camera->sample || runtime_camera->fingerprint.empty())) {
        return PlanDecodeError{"camera_sample_provider",
            "runtime camera sampling requires a callable provider and stable fingerprint",
            "invalid_camera_sample_provider", "render-plan-camera"};
    }
    if (runtime_camera != nullptr && !plan.camera) {
        return PlanDecodeError{"camera_sample_provider",
            "runtime camera sampling requires a camera descriptor for static lens and clipping defaults",
            "camera_sample_provider_requires_camera", "render-plan-camera"};
    }
    if (runtime_camera != nullptr && plan.camera_samples) {
        return PlanDecodeError{"camera_sample_provider",
            "runtime camera sampling cannot be combined with serialized camera samples; provide one resolved camera source",
            "runtime_camera_conflicts_with_samples", "render-plan-camera"};
    }
    return std::nullopt;
}

} // namespace chronon3d::render_plan::compiler_entry_detail
