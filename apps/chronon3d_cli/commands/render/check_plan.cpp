#include "check_plan.hpp"

#include "render_plan_inspection.hpp"
#include "render_plan_preparation.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

namespace chronon3d::cli::check {
namespace {

[[nodiscard]] bool is_blank(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](const unsigned char ch) {
        return std::isspace(ch) != 0;
    });
}

[[nodiscard]] std::string layer_label(const PlanLayerFacts& layer) {
    return layer.id.empty() ? std::string{"<unnamed>"} : layer.id;
}

} // namespace

PlanScanResult scan_plan(const std::string& plan_path,
                         const std::string& assets_root) {
    PlanScanResult result;
    try {
        RenderPlanPreparationOptions options;
        options.input = plan_path;
        options.assets_root = assets_root;
        if (!options.assets_root.empty()) {
            // The resolver rejects relative mounts; accept a CLI-relative root
            // and normalize it once, before preparation sees it.
            std::error_code error;
            const auto absolute =
                std::filesystem::absolute(options.assets_root, error);
            if (error) {
                result.error = "could not resolve assets root '" +
                               assets_root + "': " + error.message();
                return result;
            }
            options.assets_root = absolute.string();
        }

        auto prepared = prepare_render_plan(options);
        if (!prepared) {
            const auto& error = prepared.error();
            result.error = error.path.empty()
                ? error.message
                : error.path + ": " + error.message;
            return result;
        }

        const auto& context = prepared.value();
        const auto inspection = build_render_plan_inspection(context);
        if (inspection.layers.size() != context.decoded.layers.size()) {
            result.error = "resolved inspection and decoded plan disagree on layer count";
            return result;
        }

        result.facts.emplace();
        auto& facts = *result.facts;
        facts.canvas_width = inspection.canvas.width;
        facts.canvas_height = inspection.canvas.height;

        for (std::size_t index = 0; index < context.decoded.layers.size(); ++index) {
            const auto& decoded = context.decoded.layers[index];
            const auto& resolved = inspection.layers[index];
            PlanLayerFacts layer;
            layer.id = decoded.id.empty() ? resolved.id : decoded.id;
            layer.type = resolved.type;
            layer.text = decoded.text;
            layer.font_resolved = resolved.font.resolved;
            layer.font_asset = resolved.font.asset;
            layer.font_family = resolved.font.family;
            layer.font_size = resolved.font_size;
            layer.box_width = resolved.layout.width;
            layer.box_height = resolved.layout.height;
            layer.safe_insets = decoded.safe_insets;
            facts.layers.push_back(std::move(layer));
        }
        return result;
    } catch (const std::exception& error) {
        result.error = error.what();
        return result;
    }
}

std::vector<Finding> analyze_plan(const PlanFacts& facts,
                                  const Thresholds& thresholds) {
    std::vector<Finding> findings;
    const double height_scale = facts.canvas_height > 0
        ? static_cast<double>(facts.canvas_height) /
          std::max(1.0, thresholds.min_font_reference_height)
        : 1.0;
    const double effective_min_font =
        std::max(1.0, thresholds.min_font_px * height_scale);

    for (const auto& layer : facts.layers) {
        if (layer.type != "text" && layer.type != "Text") continue;
        const std::string label = layer_label(layer);

        if (is_blank(layer.text)) {
            Finding finding;
            finding.id = "text.empty";
            finding.category = "plan";
            finding.severity = Severity::P2;
            finding.message = "text layer '" + label + "' has no visible content";
            finding.fix = "set layer.text or remove the layer";
            finding.details = {{"layer", label}};
            findings.push_back(std::move(finding));
        }

        if (!layer.font_resolved) {
            Finding finding;
            finding.id = "text.font_unresolved";
            finding.category = "plan";
            finding.severity = Severity::P0;
            finding.message = "text layer '" + label + "' has no resolvable font";
            finding.fix = "set style.font or style.font_family to an asset the "
                          "resolver can load";
            finding.details = {
                {"layer", label},
                {"font_asset", layer.font_asset},
                {"font_family", layer.font_family},
            };
            findings.push_back(std::move(finding));
        }

        if (!layer.font_size || *layer.font_size <= 0.0f) {
            Finding finding;
            finding.id = "text.font_size_missing";
            finding.category = "plan";
            finding.severity = Severity::P1;
            finding.message = "text layer '" + label + "' has no positive font size";
            finding.fix = "set style.font_size (or a text fit mode that resolves it)";
            finding.details = {{"layer", label}};
            findings.push_back(std::move(finding));
        } else if (static_cast<double>(*layer.font_size) < effective_min_font) {
            Finding finding;
            finding.id = "text.too_small";
            finding.category = "plan";
            finding.severity = Severity::P1;
            finding.message = "text layer '" + label + "' is below the readable size floor";
            finding.fix = "raise style.font_size or lower --min-font-px if the "
                          "output is intentionally small";
            finding.details = {
                {"layer", label},
                {"font_size_px", *layer.font_size},
                {"threshold_px", effective_min_font},
                {"canvas_height", facts.canvas_height},
            };
            findings.push_back(std::move(finding));
        }

        if ((layer.box_width && *layer.box_width <= 0.0f) ||
            (layer.box_height && *layer.box_height <= 0.0f)) {
            Finding finding;
            finding.id = "text.box_invalid";
            finding.category = "plan";
            finding.severity = Severity::P2;
            finding.message = "text layer '" + label + "' declares an empty box";
            finding.fix = "remove the empty layout.width/height or set a positive size";
            finding.details = {
                {"layer", label},
                {"width", layer.box_width ? *layer.box_width : 0.0f},
                {"height", layer.box_height ? *layer.box_height : 0.0f},
            };
            findings.push_back(std::move(finding));
        }

        if (layer.safe_insets) {
            const auto& inset = *layer.safe_insets;
            const bool negative = std::any_of(
                inset.begin(), inset.end(),
                [](const float value) { return value < 0.0f; });
            const bool exceeds = facts.canvas_width > 0 && facts.canvas_height > 0 &&
                (inset[0] + inset[2] >= static_cast<float>(facts.canvas_height) ||
                 inset[1] + inset[3] >= static_cast<float>(facts.canvas_width));
            if (negative || exceeds) {
                Finding finding;
                finding.id = "plan.safe_insets_invalid";
                finding.category = "plan";
                finding.severity = Severity::P1;
                finding.message = "text layer '" + label + "' declares invalid safe insets";
                finding.fix = "use non-negative insets whose opposite pairs sum to "
                              "less than the canvas";
                finding.details = {
                    {"layer", label},
                    {"top", inset[0]}, {"right", inset[1]},
                    {"bottom", inset[2]}, {"left", inset[3]},
                    {"canvas_width", facts.canvas_width},
                    {"canvas_height", facts.canvas_height},
                };
                findings.push_back(std::move(finding));
            }
        }
    }

    std::sort(findings.begin(), findings.end(),
        [](const Finding& a, const Finding& b) {
            const int severity_a = static_cast<int>(a.severity);
            const int severity_b = static_cast<int>(b.severity);
            if (severity_a != severity_b) return severity_a < severity_b;
            return a.message < b.message;
        });
    return findings;
}

} // namespace chronon3d::cli::check
