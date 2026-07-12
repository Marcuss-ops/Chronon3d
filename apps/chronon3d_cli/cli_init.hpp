#pragma once

// ---------------------------------------------------------------------------
// CLI Initialisation Hooks — thin orchestrator (TICKET-CLI-ISOLATE-RUNTIME-DEV)
//
// PR 2: Explicit registration — compositions are added directly to the
// CompositionRegistry via register_content_modules() and
// register_builtin_compositions() which now take a registry reference.
//
// PR 4: AssetRegistry de-singletonized — thread-local pointer set
// unconditionally so asset resolution works with or without content modules.
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV: composition registration is now split into
// 3 layers (see register_compositions.hpp):
//   1. register_runtime_compositions — production runtime surface
//   2. register_content_compositions — content module bridge
//   3. register_dev_compositions     — DEV-only test/floor compositions
// This file is now a thin orchestrator that calls those 3 functions in order.
// It does NOT include any test/golden header (the production CLI must remain
// free of tests/ source dependencies).
// ---------------------------------------------------------------------------

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>

// The 3 composition registration layers (see register_compositions.hpp).
#include "register_compositions.hpp"

// StyleRegistry + MotionRegistry authoring helpers — content-module ambient
// authoring accessors.  NOT composition registrations; kept here for the
// legacy inline `cli_style_registry()` / `cli_motion_registry()` accessors
// consumed by register_content_compositions (when CHRONON3D_BUILD_CONTENT).
#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
#include <content/register_content_modules.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/authoring/detail/basic_registry.hpp>            // PR 3.5
#include <chronon3d/authoring/style_registry.hpp>                    // PR 3.5
#include <chronon3d/authoring/motion_registry.hpp>                    // PR 3.5
#endif

namespace chronon3d::cli {

// cli_asset_registry() REMOVED — the global mutable static AssetRegistry
// was a concurrency hazard (daemon, watch mode, parallel render jobs).
// AssetRegistry is now created in main() and threaded through CliContext.
// Per-renderer asset mounting happens in create_renderer() via
// renderer->runtime().assets().mount(cwd) + resolver().mount(cwd).

/// PR 3.5 — returns the CLI-wide static StyleRegistry + MotionRegistry.
/// Created once, shared between authoring-time text builders. Host code
/// populates these via dedicated API when ready; the default factories
/// register nothing so unintended wildcards never resolve.
///
/// Conditionally compiled: the inline definitions reference
/// `authoring::StyleRegistry` / `authoring::MotionRegistry`, which are
/// only forward-visible when `CHRONON3D_BUILD_CONTENT` or
/// `CHRONON3D_BUILD_DIAGNOSTICS` is defined.  Without the macros the type
/// names are unknown and the pre-existing inline bodies fail to compile,
/// so the definitions are guarded by the same #if.
#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
inline authoring::StyleRegistry&  cli_style_registry() {
    static authoring::StyleRegistry  reg;
    return reg;
}
inline authoring::MotionRegistry& cli_motion_registry() {
    static authoring::MotionRegistry reg;
    return reg;
}
#endif

/// Register all compositions into the given registry.
/// Per TICKET-CLI-ISOLATE-RUNTIME-DEV: 3-layer split
///   1. register_runtime_compositions  (always)
///   2. register_content_compositions  (always when content/diagnostics)
///   3. register_dev_compositions      (only when CHRONON3D_BUILD_CLI_DEV)
/// The `assets` parameter is accepted for backward compatibility with the
/// caller's signature; composition registration does not need assets
/// (asset resolution happens at render time, not at registration time).
/// Safe to call multiple times.
inline void init_compositions(CompositionRegistry& registry, AssetRegistry& /*assets*/) {
    (void)0;  // assets retained for caller-compat; not used by register_*_compositions.

    // Layer 1 — production runtime (ChrononGlowFinalAE + builtins).
    register_runtime_compositions(registry);

    // Layer 2 — content module bridge (cinematic showcases, examples,
    // certifications, launches, text placement, etc.).
    register_content_compositions(registry);

#ifdef CHRONON3D_BUILD_CLI_DEV
    // Layer 3 — DEV/test compositions (PipelineParityCanary, AE_CAM_*,
    // CameraTruth*, AnimTypewriterGlowNoGlow, glow A/B siblings, etc.).
    // Production CLI (CHRONON3D_BUILD_CLI_DEV=OFF) does NOT call this.
    register_dev_compositions(registry);
#endif
}

} // namespace chronon3d::cli
