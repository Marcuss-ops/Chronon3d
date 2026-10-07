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
