#include "daemon_service_ipc_protocol.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>

namespace chronon3d::cli {

PlanDimensions extract_plan_dimensions(
    const std::filesystem::path& plan_path,
    std::uint32_t& width,
    std::uint32_t& height) {
    if (width > 0 && height > 0) return PlanDimensions::NotRequested;
    if (plan_path.empty()) return PlanDimensions::NotRequested;

    std::ifstream file(plan_path);
    if (!file.is_open()) {
        spdlog::error("[daemon] plan dimensions unavailable: cannot open '{}'",
                      plan_path.string());
        return PlanDimensions::Unreadable;
    }
    try {
        nlohmann::json plan_json;
        file >> plan_json;
        if (plan_json.contains("canvas") && plan_json["canvas"].is_object()) {
            width = plan_json["canvas"].value("width", 0U);
            height = plan_json["canvas"].value("height", 0U);
        } else if (plan_json.contains("width") && plan_json.contains("height")) {
            width = plan_json.value("width", 0U);
            height = plan_json.value("height", 0U);
        }
    } catch (const std::exception& exception) {
        spdlog::error("[daemon] plan dimensions unavailable: '{}' is not a readable "
                      "plan ({})", plan_path.string(), exception.what());
        width = 0;
        height = 0;
        return PlanDimensions::Unreadable;
    }
    return (width > 0 && height > 0) ? PlanDimensions::Resolved
                                     : PlanDimensions::NoCanvasField;
}

} // namespace chronon3d::cli
