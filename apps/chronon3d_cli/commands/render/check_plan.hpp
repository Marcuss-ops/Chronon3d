#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// check_plan.hpp — text/geometry findings from a resolved RenderPlan.
//
// The plan is decoded and prepared through the canonical
// `prepare_render_plan()` path, so the checks see the same resolved font
// asset and layout values the renderer consumes.
// ─────────────────────────────────────────────────────────────────────────────

#include "check_findings.hpp"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace chronon3d::cli::check {

struct PlanLayerFacts {
    std::string id;
    std::string type;
    std::string text;
    bool font_resolved{false};
    std::string font_asset;
    std::string font_family;
    std::optional<float> font_size;
    std::optional<float> box_width;
    std::optional<float> box_height;
    std::optional<std::array<float, 4>> safe_insets;
};

struct PlanFacts {
    int canvas_width{0};
    int canvas_height{0};
    std::vector<PlanLayerFacts> layers;
};

struct PlanScanResult {
    std::optional<PlanFacts> facts;
    std::string error;
};

[[nodiscard]] PlanScanResult scan_plan(const std::string& plan_path,
                                       const std::string& assets_root);

[[nodiscard]] std::vector<Finding> analyze_plan(const PlanFacts& facts,
                                                const Thresholds& thresholds);

} // namespace chronon3d::cli::check
