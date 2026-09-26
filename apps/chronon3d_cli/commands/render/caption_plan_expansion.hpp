#pragma once

#include <filesystem>
#include <string>

namespace chronon3d::cli {

struct CaptionPlanExpansion {
    std::filesystem::path plan_path;
    std::string error;
    ~CaptionPlanExpansion();
    CaptionPlanExpansion() = default;
    CaptionPlanExpansion(CaptionPlanExpansion&& other) noexcept
        : plan_path(std::move(other.plan_path)), error(std::move(other.error)) {
        other.plan_path.clear();
    }
    CaptionPlanExpansion& operator=(CaptionPlanExpansion&& other) noexcept;
    CaptionPlanExpansion(const CaptionPlanExpansion&) = delete;
    CaptionPlanExpansion& operator=(const CaptionPlanExpansion&) = delete;
};

[[nodiscard]] CaptionPlanExpansion expand_caption_plan(
    const std::filesystem::path& plan,
    const std::filesystem::path& captions,
    const std::string& preset,
    const std::string& font = {});

} // namespace chronon3d::cli
