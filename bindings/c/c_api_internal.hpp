#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Internal shared declarations for the C API implementation.
// Not part of the public API — only included by .cpp files in bindings/c/.
// ═════════════════════════════════════════════════════════════════════════════

#include "chronon3d/c_api/chronon3d.h"
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

// ── Context struct ─────────────────────────────────────────────────────
// Defined here (not just forward-declared) so that all C API translation
// units can access ctx->last_error and ctx->registry.
struct chronon_context {
    std::string last_error;
    std::unique_ptr<chronon3d::CompositionRegistry> registry;
};

// ── Thread-local error ────────────────────────────────────────────────
// Global error string for failures that occur before a context exists
// (e.g. chronon_create_context failure).
extern thread_local std::string g_c_api_last_error;

// ── Compile helper (c_api_compile.cpp) ─────────────────────────────────
// Tries to parse |input| as JSON (→TOML), then as TOML/specscene, and
// finally as a composition name in |registry|.  Returns the composition
// on success or std::nullopt with diagnostics populated on failure.
std::optional<chronon3d::Composition> c_api_compile_content(
    const std::string& input,
    std::vector<std::string>& diagnostics,
    const chronon_render_options* options,
    chronon3d::CompositionRegistry* registry);

// ── Render helper (c_api_render.cpp) ───────────────────────────────────
// Renders |comp| at the frame specified in |options|, saves the result
// to |output_png_path| and returns a chronon_status code.
chronon_status c_api_render_and_save(
    chronon_context* ctx,
    chronon3d::Composition& comp,
    const chronon_render_options* options,
    const char* output_png_path);
