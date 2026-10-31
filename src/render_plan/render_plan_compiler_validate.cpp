#include "render_plan_compiler_entry_detail.hpp"
#include <chronon3d/render_plan/camera_contract.hpp>
#include <chronon3d/render_plan/resolved_transform_samples.hpp>

namespace chronon3d::render_plan::compiler_entry_detail {

std::optional<PlanDecodeError> validate_compile_request(
    const RenderPlan& plan,
    const RuntimeCameraSampleProvider* runtime_camera) {
    for (const auto& layer : plan.layers)
        if (const auto problem = transform_samples_problem(layer))
            return PlanDecodeError{"layers." + layer.id + ".transform_samples", *problem, "invalid_resolved_transform_samples", "render-plan-motion"};
    if (plan.camera) {
        if (const auto problem = camera_contract_problem(*plan.camera))
            return PlanDecodeError{"camera", *problem, "unsupported_camera_contract", "render-plan-camera"};
    }
    if (plan.camera_samples) {
        for (const auto& sample : *plan.camera_samples)
            if (const auto problem = camera_contract_problem(sample.camera))
                return PlanDecodeError{"camera_samples", *problem, "unsupported_camera_contract", "render-plan-camera"};
    }
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
