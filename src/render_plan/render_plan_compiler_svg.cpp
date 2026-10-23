#include <chronon3d/render_plan/render_plan_compiler.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/svg_path_loader.hpp>
#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/graphics/shape_style/fill_style.hpp>
#include <chronon3d/graphics/shape_style/stroke_style.hpp>
#include <chronon3d/scene/model/shape/path.hpp>
#include <chronon3d/scene/model/shape/path_operator.hpp>
#include <chronon3d/text/text_path_composer.hpp>
#include <chronon3d/text/font_engine.hpp>
#include <chronon3d/backends/text/text_unicode_grapheme.hpp>
#include "render_plan_compiler_detail.hpp"
#include "../text/unicode/unicode_script.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace chronon3d::render_plan {
namespace {
text::unicode::Script first_strong_unicode_script(std::string_view value) {
    return text::unicode::first_strong_script(value);
}
} // namespace

namespace detail {
std::optional<PlanDecodeError> prepare_svg_layers(
    RenderPlan& plan, chronon3d::assets::AssetResolver& resolver) {
#if defined(CHRONON3D_ENABLE_SVG)
    std::vector<std::pair<chronon3d::assets::SvgTextRun, LayerPlan>> pending_svg_text_layers;
    std::vector<SvgTextMetrics> pending_svg_text_metrics;
    std::vector<bool> pending_svg_text_boundary_before;
    std::unique_ptr<chronon3d::FontEngine> svg_text_font_engine;
    for (std::size_t layer_index = 0; layer_index < plan.layers.size(); ++layer_index) {
        auto& layer = plan.layers[layer_index];
        if (!layer.shape || !layer.shape->svg_asset) continue;
        const auto logical = *layer.shape->svg_asset;
        const auto resolved = resolver.resolve_logical(logical);
        if (!resolved)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                "SVG asset could not be resolved within the mounted asset root",
                "svg_asset_unresolved", "render-plan-svg"};
        const auto imported = chronon3d::assets::load_svg_document_file(resolved->string());
        if (!imported.ok)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                imported.error, "svg_import_failed", "render-plan-svg"};
        if (imported.elements.empty() && imported.text_runs.empty())
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                "SVG contains no path geometry or text", "svg_empty_geometry", "render-plan-svg"};
        if (imported.elements.size() + imported.text_runs.size() > 4096)
            return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                "SVG styled path/text run count exceeds 4096", "svg_resource_limit", "render-plan-svg"};
        if (imported.root_gaussian_blur) {
            if (!std::isfinite(*imported.root_gaussian_blur) ||
                *imported.root_gaussian_blur < 0.0f ||
                *imported.root_gaussian_blur > 256.0f)
                return PlanDecodeError{"layers[" + std::to_string(layer_index) + "].shape.asset",
                    "SVG root Gaussian blur is outside the supported radius bounds",
                    "svg_unsupported_filter", "render-plan-svg"};
            LayerPlan::EffectPlan native_blur;
            native_blur.kind = LayerPlan::EffectKindPlan::GaussianBlur;
            native_blur.radius = *imported.root_gaussian_blur;
            layer.effects.insert(layer.effects.begin(), std::move(native_blur));
        }
        auto& shape = *layer.shape;
        if (auto error = append_svg_imported_elements(
                shape, imported.elements, plan.canvas.fps.fps(), layer_index))
            return error;
        std::vector<std::size_t> current_text_layers;
        current_text_layers.reserve(imported.text_runs.size());
        std::vector<SvgTextMetrics> text_metrics(imported.text_runs.size());
        std::vector<bool> text_chunk_boundary_safe(imported.text_runs.size(), true);
        std::vector<text::unicode::Script> effective_text_scripts(
            imported.text_runs.size(), text::unicode::unknown_script);
        if (auto error = measure_svg_text_runs(
                imported.text_runs, layer_index, resolver, svg_text_font_engine,
                text_metrics))
            return error;
        if (auto error = shape_svg_text_runs(
                imported.text_runs, layer.id, resolver, svg_text_font_engine,
                text_metrics, text_chunk_boundary_safe))
            return error;
        if (auto error = append_svg_text_runs(
                layer, plan, resolver, layer_index, shape,
                imported.text_runs, imported.elements, svg_text_font_engine,
                text_metrics, text_chunk_boundary_safe, current_text_layers,
                pending_svg_text_layers, pending_svg_text_metrics,
                pending_svg_text_boundary_before))
            return error;
        if (auto error = validate_svg_path_operator_targets(shape, layer_index))
            return error;
        if (auto error = attach_svg_element_animations(
                shape, layer_index, current_text_layers, pending_svg_text_layers))
            return error;
        if (auto error = apply_svg_document_mask(layer, imported.vector_mask, layer_index))
            return error;
    }
    if (auto error = append_merged_svg_text_layers(
            plan, pending_svg_text_layers, pending_svg_text_metrics,
            pending_svg_text_boundary_before))
        return error;
#else
    for (std::size_t index = 0; index < plan.layers.size(); ++index)
        if (plan.layers[index].shape && plan.layers[index].shape->svg_asset)
            return PlanDecodeError{"layers[" + std::to_string(index) + "].shape.asset",
                "Chronon was built without SVG import support", "svg_import_unavailable",
                "render-plan-svg"};
#endif
    return std::nullopt;
}
} // namespace detail
} // namespace chronon3d::render_plan
