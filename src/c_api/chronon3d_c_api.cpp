#include "chronon3d/c_api/chronon3d.h"
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/runtime/timeline_evaluator.hpp>
#include <chronon3d/specscene/model/specscene.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#ifdef CHRONON3D_BUILD_CONTENT
#include <content/register_content_modules.hpp>
#endif
#include <nlohmann/json.hpp>
#include <toml++/toml.h>
#include "../specscene/parser/specscene_parsers.hpp"

#include <string>
#include <exception>
#include <fstream>
#include <filesystem>
#include <memory>

namespace {
// Thread-local global error for failures that occur before a context exists
// (e.g. chronon_create_context failure).  chronon_last_error(NULL) reads this.
thread_local std::string g_last_error;
} // namespace

struct chronon_context {
    std::string last_error;
    std::unique_ptr<chronon3d::CompositionRegistry> registry;
};

namespace {

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

std::optional<chronon3d::Composition> compile_json_string(const std::string& json_str, std::vector<std::string>& diagnostics, const chronon_render_options* options) {
    try {
        auto j = nlohmann::json::parse(json_str);
        toml::table root = convert_json_to_toml(j);
        auto doc = chronon3d::specscene::parse_document(root, &diagnostics);
        if (!doc) return std::nullopt;

        chronon3d::CompositionSpec spec;
        spec.name = doc->scene.name;
        spec.width = (options && options->width > 0) ? options->width : doc->scene.width;
        spec.height = (options && options->height > 0) ? options->height : doc->scene.height;
        spec.frame_rate = doc->scene.frame_rate;
        spec.duration = doc->scene.duration;

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

} // namespace

namespace {

chronon_status render_and_save(
    chronon_context* ctx,
    chronon3d::Composition& comp,
    const chronon_render_options* options,
    const char* output_png_path
) {
    chronon3d::SoftwareRenderer renderer;
    chronon3d::Frame target_frame = options ? options->frame : 0;

    std::shared_ptr<chronon3d::Framebuffer> fb;
    if (options && options->width > 0 && options->height > 0) {
        auto scene = comp.evaluate(target_frame);
        fb = renderer.render_scene(scene, comp.camera, options->width, options->height);
    } else {
        fb = renderer.render_frame(comp, target_frame);
    }

    if (!fb) {
        ctx->last_error = "Renderer returned null framebuffer";
        return CHRONON_ERROR_RENDER_FAILED;
    }

    if (!chronon3d::save_png(*fb, output_png_path)) {
        ctx->last_error = "Failed to save output PNG image";
        return CHRONON_ERROR_IO_FAILED;
    }

    return CHRONON_OK;
}

} // namespace

extern "C" {

chronon_context* chronon_create_context(void) {
    try {
#ifdef CHRONON3D_BUILD_CONTENT
        // Ensure content ExtensionModules are registered before CompositionRegistry is built.
        chronon3d::register_content_modules();
#endif

        // Register built-in compositions (DarkGridBackground, GridCleanBackground,
        // CameraImageClip) before CompositionRegistry is built so that populate()
        // finds the factories.  Safe to call multiple times.
        chronon3d::register_builtin_compositions();

        auto* ctx = new chronon_context();
        ctx->registry = std::make_unique<chronon3d::CompositionRegistry>();
        return ctx;
    } catch (const std::exception& e) {
        g_last_error = std::string("chronon_create_context failed: ") + e.what();
        return nullptr;
    } catch (...) {
        g_last_error = "chronon_create_context failed: unknown exception";
        return nullptr;
    }
}

void chronon_destroy_context(chronon_context* ctx) {
    delete ctx;
}

const char* chronon_last_error(chronon_context* ctx) {
    if (!ctx) {
        return g_last_error.empty() ? "Invalid context" : g_last_error.c_str();
    }
    return ctx->last_error.c_str();
}

const char* chronon_version_string(void) {
    return "Chronon3D 0.1.0";
}

chronon_status chronon_render_json_string(
    chronon_context* ctx,
    const char* json_string,
    const char* output_png_path,
    const chronon_render_options* options
) {
    if (!ctx || !json_string || !output_png_path) {
        if (ctx) ctx->last_error = "Invalid arguments";
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }

    try {
        std::vector<std::string> diagnostics;
        std::optional<chronon3d::Composition> compOpt;

        std::string content_str = json_string;
        size_t first_non_ws = content_str.find_first_not_of(" \t\r\n");
        bool is_json = false;
        if (first_non_ws != std::string::npos) {
            is_json = (content_str[first_non_ws] == '{' || content_str[first_non_ws] == '[');
        }

        if (is_json) {
            compOpt = compile_json_string(content_str, diagnostics, options);
        } else {
            // Try parsing directly as TOML / specscene (Primary Option)
            try {
                const auto parsed = toml::parse(content_str);
                auto doc = chronon3d::specscene::parse_document(parsed, &diagnostics);
                if (doc) {
                    chronon3d::CompositionSpec spec;
                    spec.name = doc->scene.name;
                    spec.width = (options && options->width > 0) ? options->width : doc->scene.width;
                    spec.height = (options && options->height > 0) ? options->height : doc->scene.height;
                    spec.frame_rate = doc->scene.frame_rate;
                    spec.duration = doc->scene.duration;

                    chronon3d::SceneDescription scene = std::move(doc->scene);
                    chronon3d::Composition comp(spec, [scene = std::move(scene)](const chronon3d::FrameContext& ctx) {
                        chronon3d::TimelineEvaluator evaluator;
                        return evaluator.evaluate(scene, ctx.frame, ctx.resource);
                    });
                    
                    if (doc->has_render_camera) {
                        comp.camera = doc->render_camera;
                    }
                    compOpt = std::move(comp);
                }
            } catch (const std::exception& e) {
                diagnostics.push_back(std::string("TOML/Specscene parse failed: ") + e.what());
            }
        }

        if (!compOpt) {
            // Fallback: Check if it's just a composition name in the registry
            std::string possible_name = json_string;
            if (ctx->registry && ctx->registry->contains(possible_name)) {
                compOpt = ctx->registry->create(possible_name);
            } else {
                std::string err = "Failed to parse specscene/TOML (or JSON fallback) and find composition in registry:";
                for (const auto& d : diagnostics) {
                    err += "\n  " + d;
                }
                ctx->last_error = err;
                return CHRONON_ERROR_PARSE_FAILED;
            }
        }

        auto& comp = *compOpt;

        return render_and_save(ctx, comp, options, output_png_path);
    } catch (const std::exception& e) {
        ctx->last_error = e.what();
        return CHRONON_ERROR_RENDER_FAILED;
    } catch (...) {
        ctx->last_error = "Unknown render error";
        return CHRONON_ERROR_UNKNOWN;
    }
}

chronon_status chronon_render_json_file(
    chronon_context* ctx,
    const char* json_path,
    const char* output_png_path,
    const chronon_render_options* options
) {
    if (!ctx || !json_path || !output_png_path) {
        if (ctx) ctx->last_error = "Invalid arguments";
        return CHRONON_ERROR_INVALID_ARGUMENT;
    }

    try {
        std::string path_str = json_path;
        if (!std::filesystem::exists(path_str)) {
            // Check if it's a registered composition ID instead of a file
            if (ctx->registry && ctx->registry->contains(path_str)) {
                auto comp = ctx->registry->create(path_str);
                return render_and_save(ctx, comp, options, output_png_path);
            }
            ctx->last_error = "File does not exist: " + path_str;
            return CHRONON_ERROR_IO_FAILED;
        }

        // Try reading the file content
        std::ifstream ifs(path_str);
        if (!ifs.is_open()) {
            ctx->last_error = "Failed to open file: " + path_str;
            return CHRONON_ERROR_IO_FAILED;
        }

        std::string file_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return chronon_render_json_string(ctx, file_content.c_str(), output_png_path, options);

    } catch (const std::exception& e) {
        ctx->last_error = e.what();
        return CHRONON_ERROR_RENDER_FAILED;
    } catch (...) {
        ctx->last_error = "Unknown render error";
        return CHRONON_ERROR_UNKNOWN;
    }
}

}
