#include "render_plan_compiler_detail.hpp"
#include <algorithm>
#include <optional>
#include <string>

namespace chronon3d::render_plan::detail {
std::optional<PlanDecodeError> validate_svg_path_operator_targets(
    const ShapeContentPlan& shape, std::size_t layer_index) {
        for (const auto& operation : shape.operators) {
            if (operation.target_id.empty()) continue;
            std::size_t exact_matches = 0;
            std::size_t group_matches = 0;
            for (const auto& subpath : shape.imported_subpaths) {
                exact_matches += subpath.source_id == operation.target_id;
                group_matches += std::find(subpath.source_group_ids.begin(),
                    subpath.source_group_ids.end(), operation.target_id) !=
                    subpath.source_group_ids.end();
            }
            if (exact_matches > 1)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.operators", "SVG path operator target id is ambiguous after <use> expansion: " +
                    operation.target_id, "svg_target_ambiguous", "render-plan-svg"};
            if (exact_matches == 0 && group_matches == 0)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.operators", "SVG path operator target does not resolve: " +
                    operation.target_id, "svg_target_missing", "render-plan-svg"};
            if ((operation.kind == PathOperatorKindPlan::Merge ||
                 operation.kind == PathOperatorKindPlan::BooleanUnion ||
                 operation.kind == PathOperatorKindPlan::BooleanDifference ||
                 operation.kind == PathOperatorKindPlan::BooleanIntersection ||
                 operation.kind == PathOperatorKindPlan::BooleanXor ||
                 operation.kind == PathOperatorKindPlan::Morph) && exact_matches != 1)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) +
                    "].shape.operators", "SVG Merge/BooleanUnion/Morph requires an exact path element id, not a group target",
                    "svg_target_property_invalid", "render-plan-svg"};
        }
    return std::nullopt;
}
} // namespace chronon3d::render_plan::detail
