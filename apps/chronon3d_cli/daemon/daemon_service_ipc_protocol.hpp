#pragma once

#include <cstdint>
#include <filesystem>

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

} // namespace chronon3d::cli
