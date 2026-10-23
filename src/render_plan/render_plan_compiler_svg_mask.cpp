#include "render_plan_compiler_detail.hpp"
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace chronon3d::render_plan::detail {
std::optional<PlanDecodeError> apply_svg_document_mask(
    LayerPlan& layer, const std::optional<chronon3d::PathShape>& vector_mask,
    std::size_t layer_index) {
    if (!vector_mask) return std::nullopt;
            if (layer.path_mask || !layer.masks.empty())
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG vector clip/mask cannot be combined with a render-plan layer mask",
                    "svg_mask_conflict", "render-plan-svg"};
            LayerPlan::PathMaskPlan mask;
            mask.type = "path";
            mask.mode = "add";
            for (const auto& command : vector_mask->commands) {
                ShapePathCommandPlan lowered;
                const auto lower_point = [&](Vec2 point, std::array<float, 2>& destination) {
                    if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                        std::abs(point.x) > 100000.0f || std::abs(point.y) > 100000.0f)
                        throw std::runtime_error("SVG mask coordinate exceeds finite resource bounds");
                    destination = {std::round(point.x * 10000.0f) / 10000.0f,
                                   std::round(point.y * 10000.0f) / 10000.0f};
                };
                switch (command.type) {
                    case PathCommandType::MoveTo: lowered.type = ShapePathCommandTypePlan::MoveTo; lower_point(command.p0, lowered.point); break;
                    case PathCommandType::LineTo: lowered.type = ShapePathCommandTypePlan::LineTo; lower_point(command.p0, lowered.point); break;
                    case PathCommandType::QuadraticTo: lowered.type = ShapePathCommandTypePlan::QuadraticTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); break;
                    case PathCommandType::CubicTo: lowered.type = ShapePathCommandTypePlan::CubicTo; lower_point(command.p0, lowered.point); lower_point(command.p1, lowered.control1); lower_point(command.p2, lowered.control2); break;
                    case PathCommandType::Close: lowered.type = ShapePathCommandTypePlan::Close; break;
                }
                mask.path.push_back(lowered);
            }
            layer.path_mask = std::move(mask);
    return std::nullopt;
}
} // namespace chronon3d::render_plan::detail
