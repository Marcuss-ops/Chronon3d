#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>

namespace chronon3d::cli {

enum class PlanDimensions : std::uint8_t {
    NotRequested,
    Resolved,
    NoCanvasField,
    Unreadable,
};

[[nodiscard]] PlanDimensions extract_plan_dimensions(
    const std::filesystem::path& plan_path,
    std::uint32_t& width,
    std::uint32_t& height);

struct DecodedRenderJobPayload;
[[nodiscard]] DecodedRenderJobPayload decode_render_job_payload(
    const nlohmann::json& request);

} // namespace chronon3d::cli
