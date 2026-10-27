#include <chronon3d/render_plan/easing_contract.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>

#include "render_plan_decoder_detail.hpp"

#include <nlohmann/json.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cctype>
#include <exception>
#include <iterator>
#include <map>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>
#include <set>

namespace easing_name = chronon3d::render_plan::detail::easing_name;
namespace interpolation_name = chronon3d::render_plan::detail::interpolation_name;

namespace {

using chronon3d::Frame;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::PlanDecodeError;





struct ResolvedVariables {
    nlohmann::json plan;
    std::vector<std::string> unused;
};

ResolvedVariables resolve_variables(const nlohmann::json& source) {
    ResolvedVariables result{source, {}};
    if (!source.contains("variables")) return result;
    const auto& raw_variables = source.at("variables");
    if (!raw_variables.is_object() || raw_variables.size() > 1024)
        throw std::runtime_error("variables must be an object with at most 1024 entries");
    std::unordered_map<std::string, nlohmann::json> variables;
    for (auto it = raw_variables.begin(); it != raw_variables.end(); ++it) {
        if (it.key().empty() ||
            !(std::isalpha(static_cast<unsigned char>(it.key().front())) || it.key().front() == '_') ||
            !std::all_of(it.key().begin() + 1, it.key().end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_';
            }) ||
            !(it.value().is_string() || it.value().is_number()))
            throw std::runtime_error("variables must map names to string or number values");
        variables.emplace(it.key(), it.value());
    }
    std::set<std::string> used;
    const auto resolve_string = [&](std::string value, bool typed,
                                    const std::string& path) -> nlohmann::json {
        std::optional<nlohmann::json> exact;
        std::string output;
        std::size_t cursor = 0;
        while (true) {
            const auto begin = value.find("${", cursor);
            if (begin == std::string::npos) {
                output.append(value, cursor, std::string::npos);
                break;
            }
            output.append(value, cursor, begin - cursor);
            const auto end = value.find('}', begin + 2);
            if (end == std::string::npos)
                throw std::runtime_error("unterminated variable reference in " + path);
            const auto name = value.substr(begin + 2, end - begin - 2);
            const auto found = variables.find(name);
            if (found == variables.end())
                throw std::runtime_error("unknown render variable '${" + name + "}' in " + path);
            used.insert(name);
            if (typed && begin == 0 && end + 1 == value.size()) {
                exact = found->second;
                break;
            }
            output += found->second.is_string() ? found->second.get<std::string>()
                                                : found->second.dump();
            cursor = end + 1;
        }
        if (exact) return *exact;
        return output;
    };
    const auto walk = [&](const auto& self, nlohmann::json& value,
                          const std::string& path, const std::string& parent_key) -> void {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (it.key().find("${") != std::string::npos)
                    throw std::runtime_error("variable references are not allowed in object keys");
                self(self, it.value(), path + "." + it.key(), it.key());
            }
        } else if (value.is_array()) {
            for (std::size_t i = 0; i < value.size(); ++i)
                self(self, value[i], path + "[" + std::to_string(i) + "]", parent_key);
        } else if (value.is_string()) {
            const auto text = value.get<std::string>();
            if (text.find("${") != std::string::npos) {
                if (parent_key == "property" || parent_key == "type")
                    throw std::runtime_error("render variables are not allowed in property/type names");
                value = resolve_string(text, true, path);
            }
        }
    };
    result.plan.erase("variables");
    walk(walk, result.plan, "$", {});
    for (const auto& [name, unused] : variables) {
        (void)unused;
        if (!used.contains(name)) result.unused.push_back(name);
    }
    return result;
}

}  // namespace

namespace chronon3d::render_plan {
using detail::apply_layout_stagger;
using detail::collect_final_key_timing_warnings;
using detail::find_3d_track;
using detail::validate_per_key_timing;

Result<RenderPlan, PlanDecodeError> decode_render_plan(const nlohmann::json& root) {
    ResolvedVariables resolved;
    try {
        resolved = resolve_variables(root);
    } catch (const std::exception& error) {
        return PlanDecodeError{"variables", error.what(), "invalid_render_variables", "render-plan-variables"};
    }
    const auto& resolved_root = resolved.plan;
    if (resolved_root.is_object() && resolved_root.contains("camera_animation"))
        return PlanDecodeError{"camera_animation",
            "camera_animation is retired; lower the camera to resolved camera_samples in ChrononMotion3D",
            "camera_animation_retired", "render-plan-decoder"};
    if (const auto error = detail::find_unregistered_animation_property(resolved_root))
        return *error;
    const auto validation = validate_render_plan(resolved_root);
    if (!validation.ok())
        return PlanDecodeError{"", validation.format()};

    try {
        validate_per_key_timing(resolved_root);
        RenderPlan plan;
        plan.warnings.reserve(resolved.unused.size());
        for (const auto& name : resolved.unused)
            plan.warnings.push_back("unused render variable '" + name + "'");
        if (resolved_root.contains("warnings"))
            for (const auto& warning : resolved_root.at("warnings"))
                plan.warnings.push_back(warning.get<std::string>());
        collect_final_key_timing_warnings(resolved_root, plan.warnings);
        plan.schema = resolved_root.at("schema").get<std::string>();
        plan.job_id = resolved_root.value("job_id", std::string{"chronon_plan"});
        if (const auto header_error = detail::decode_plan_globals(
                resolved_root, plan))
            return *header_error;
        if (const auto layer_error = detail::decode_plan_layers(
                resolved_root.at("layers"), plan))
            return *layer_error;

        if (const auto output_error = detail::decode_output_settings(
                resolved_root.at("output"), plan))
            return *output_error;
        if (resolved_root.contains("budget")) {
            const auto& budget = resolved_root.at("budget");
            plan.budget.max_temporal_pixels = budget.value(
                "max_temporal_pixels", plan.budget.max_temporal_pixels);
        }
        if (const auto budget_error = validate_render_budget(plan))
            return *budget_error;
        plan.content_fingerprint = compute_render_plan_content_fingerprint(plan);
        return plan;
    } catch (const std::exception& error) {
        return PlanDecodeError{"", error.what()};
    }
}

}  // namespace chronon3d::render_plan
