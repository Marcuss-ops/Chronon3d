#include "c_api_internal.hpp"

#include <chronon3d/chronon3d.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

// ═════════════════════════════════════════════════════════════════════════════
// Public C API — dispatchers that delegate to c_api_compile_content() and
// c_api_render_and_save() from the companion .cpp files.
// ═════════════════════════════════════════════════════════════════════════════

extern "C" {

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
        std::string content_str = json_string;

        std::optional<chronon3d::Composition> compOpt =
            c_api_compile_content(content_str, diagnostics, options, ctx->registry.get());

        if (!compOpt) {
            std::string err = "Failed to parse content and find composition in registry:";
            for (const auto& d : diagnostics) {
                err += "\n  " + d;
            }
            ctx->last_error = err;
            return CHRONON_ERROR_PARSE_FAILED;
        }

        return c_api_render_and_save(ctx, *compOpt, options, output_png_path);
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
                return c_api_render_and_save(ctx, comp, options, output_png_path);
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

} // extern "C"
