#include "c_api_internal.hpp"

#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#ifdef CHRONON3D_BUILD_CONTENT
#include <content/register_content_modules.hpp>
#endif

#include <exception>
#include <string>

// ── Thread-local global error ──────────────────────────────────────────
thread_local std::string g_c_api_last_error;

// ── Context ────────────────────────────────────────────────────────────

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
        g_c_api_last_error = std::string("chronon_create_context failed: ") + e.what();
        return nullptr;
    } catch (...) {
        g_c_api_last_error = "chronon_create_context failed: unknown exception";
        return nullptr;
    }
}

void chronon_destroy_context(chronon_context* ctx) {
    delete ctx;
}

const char* chronon_last_error(chronon_context* ctx) {
    if (!ctx) {
        return g_c_api_last_error.empty() ? "Invalid context" : g_c_api_last_error.c_str();
    }
    return ctx->last_error.c_str();
}

const char* chronon_version_string(void) {
    return "Chronon3D 0.1.0";
}

} // extern "C"
