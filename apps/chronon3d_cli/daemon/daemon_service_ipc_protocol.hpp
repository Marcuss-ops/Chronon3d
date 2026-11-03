#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include "daemon_render_job_payload.hpp"
#include <nlohmann/json_fwd.hpp>

namespace chronon3d::cli {

enum class PlanDimensions : std::uint8_t {
    NotRequested,
    Resolved,
    NoCanvasField,
    Unreadable,
};

/// Admission-relevant facts read from the plan document BEFORE anything is
/// compiled. `requires_composition` is the one the presence of a
/// `parallel_disjoint` hint must not be allowed to override: a composited plan
/// runs on the FullGraph lane, which owns one renderer and one transient
/// surface ring per device (measured 2026-11-02: two such jobs admitted into
/// the shared domain hang or kill the daemon), so it is ONE execution domain
/// whatever the producer declares. The default is `true` because an unreadable
/// or unexpected plan must never be promoted into the shared domain.
struct PlanAdmissionShape {
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool requires_composition{true};
};

[[nodiscard]] PlanDimensions extract_plan_admission_shape(
    const std::filesystem::path& plan_path,
    PlanAdmissionShape& shape);

[[nodiscard]] PlanDimensions extract_plan_dimensions(
    const std::filesystem::path& plan_path,
    std::uint32_t& width,
    std::uint32_t& height);

struct DecodedRenderJobPayload;
[[nodiscard]] DecodedRenderJobPayload decode_render_job_payload(
    const nlohmann::json& request);
[[nodiscard]] std::vector<DecodedRenderJobPayload> decode_render_batch_payload(
    const std::string& payload);

} // namespace chronon3d::cli
