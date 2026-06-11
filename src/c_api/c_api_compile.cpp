#include "c_api_internal.hpp"

#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/specscene/model/specscene.hpp>
#include "../specscene/parser/specscene_parsers.hpp"

#include <nlohmann/json.hpp>
#include <toml++/toml.h>

#include <exception>
#include <string>

namespace {

// ── JSON → TOML conversion ────────────────────────────────────────────
toml::table convert_json_to_toml(const nlohmann::json& j) {
    toml::table tbl;
    for (auto& [key, val] : j.items()) {
        if (val.is_object()) {
            tbl.insert(key, convert_json_to_toml(val));
        } else if (val.is_array()) {
            toml::array arr;
            for (auto& item : val) {
                if (item.is_object()) {
                    arr.push_back(convert_json_to_toml(item));
                } else if (item.is_array()) {
                    // Recurse for nested arrays if needed
                } else if (item.is_string()) {
                    arr.push_back(item.get<std::string>());
                } else if (item.is_number_integer()) {
                    arr.push_back(item.get<int64_t>());
                } else if (item.is_number_float()) {
                    arr.push_back(item.get<double>());
                } else if (item.is_boolean()) {
                    arr.push_back(item.get<bool>());
                }
            }
            tbl.insert(key, std::move(arr));
        } else if (val.is_string()) {
            tbl.insert(key, val.get<std::string>());
        } else if (val.is_number_integer()) {
            tbl.insert(key, val.get<int64_t>());
        } else if (val.is_number_float()) {
            tbl.insert(key, val.get<double>());
        } else if (val.is_boolean()) {
            tbl.insert(key, val.get<bool>());
        }
    }
    return tbl;
}

// ── Compile a JSON string into a Composition ───────────────────────────
std::optional<chronon3d::Composition> compile_json_string(
    const std::string& json_str,
    std::vector<std::string>& diagnostics,
    const chronon_render_options* options)
{
    try {
        auto j = nlohmann::json::parse(json_str);
        toml::table root = convert_json_to_toml(j);
        auto doc = chronon3d::specscene::parse_document(root, &diagnostics);
        if (!doc) return std::nullopt;

        chronon3d::CompositionSpec spec;
        spec.name = doc->scene.name;
        spec.width  = (options && options->width > 0)  ? options->width  : doc->scene.width;
        spec.height = (options && options->height > 0) ? options->height : doc->scene.height;
        spec.frame_rate = doc->scene.frame_rate;
        spec.duration   = doc->scene.duration;

        chronon3d::SceneDescription scene = std::move(doc->scene);
        chronon3d::Composition comp(spec, [scene = std::move(scene)](const chronon3d::FrameContext& ctx) {
            chronon3d::TimelineEvaluator evaluator;
            return evaluator.evaluate(scene, ctx.frame, ctx.resource);
        });

        if (doc->has_render_camera) {
            comp.camera = doc->render_camera;
        }

        return comp;
    } catch (const std::exception& e) {
        diagnostics.push_back(std::string("JSON parse/compile failed: ") + e.what());
        return std::nullopt;
    }
}

// ── Compile a TOML/specscene string into a Composition ─────────────────
std::optional<chronon3d::Composition> compile_toml_string(
    const std::string& toml_str,
    std::vector<std::string>& diagnostics,
    const chronon_render_options* options)
{
    try {
        const auto parsed = toml::parse(toml_str);
        auto doc = chronon3d::specscene::parse_document(parsed, &diagnostics);
        if (!doc) return std::nullopt;

        chronon3d::CompositionSpec spec;
        spec.name = doc->scene.name;
        spec.width  = (options && options->width > 0)  ? options->width  : doc->scene.width;
        spec.height = (options && options->height > 0) ? options->height : doc->scene.height;
        spec.frame_rate = doc->scene.frame_rate;
        spec.duration   = doc->scene.duration;

        chronon3d::SceneDescription scene = std::move(doc->scene);
        chronon3d::Composition comp(spec, [scene = std::move(scene)](const chronon3d::FrameContext& ctx) {
            chronon3d::TimelineEvaluator evaluator;
            return evaluator.evaluate(scene, ctx.frame, ctx.resource);
        });

        if (doc->has_render_camera) {
            comp.camera = doc->render_camera;
        }

        return comp;
    } catch (const std::exception& e) {
        diagnostics.push_back(std::string("TOML/Specscene parse failed: ") + e.what());
        return std::nullopt;
    }
}

} // anonymous namespace

// ── Public compile entry point ─────────────────────────────────────────
std::optional<chronon3d::Composition> c_api_compile_content(
    const std::string& input,
    std::vector<std::string>& diagnostics,
    const chronon_render_options* options,
    chronon3d::CompositionRegistry* registry)
{
    // Detect format
    size_t first_non_ws = input.find_first_not_of(" \t\r\n");
    bool is_json = (first_non_ws != std::string::npos &&
                    (input[first_non_ws] == '{' || input[first_non_ws] == '['));

    std::optional<chronon3d::Composition> comp;

    if (is_json) {
        comp = compile_json_string(input, diagnostics, options);
    } else {
        comp = compile_toml_string(input, diagnostics, options);
    }

    if (!comp && registry) {
        // Fallback: check if it's just a composition name in the registry
        std::string possible_name = input;
        if (registry->contains(possible_name)) {
            comp = registry->create(possible_name);
        }
    }

    return comp;
}
