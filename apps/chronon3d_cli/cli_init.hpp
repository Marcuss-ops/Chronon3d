#pragma once

// ---------------------------------------------------------------------------
// CLI Initialisation Hooks
//
// PR 2: Explicit registration — compositions are added directly to the
// CompositionRegistry via register_content_modules() and
// register_builtin_compositions() which now take a registry reference.
//
// PR 4: AssetRegistry de-singletonized — thread-local pointer set
// unconditionally so asset resolution works with or without content modules.
// ---------------------------------------------------------------------------

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>

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

/// Returns the CLI-wide static AssetRegistry.  Created once, shared
/// between init_compositions() and render_job_setup().
inline AssetRegistry& cli_asset_registry() {
    static AssetRegistry reg;
    return reg;
}

/// PR 3.5 — returns the CLI-wide static StyleRegistry + MotionRegistry.
/// Created once, shared between authoring-time text builders. Host code
/// populates these via dedicated API when ready; the default factories
/// register nothing so unintended wildcards never resolve.
///
/// Conditionally compiled: the inline definitions reference
/// `authoring::StyleRegistry` / `authoring::MotionRegistry`, which are
/// only forward-visible when `CHRONON3D_BUILD_CONTENT` or
/// `CHRONON3D_BUILD_DIAGNOSTICS` is defined (the same #if that pulls in
/// `style_registry.hpp` / `motion_registry.hpp` / `basic_registry.hpp`
/// above).  Without the macros the type names are unknown and the
/// pre-existing inline bodies fail to compile, so the definitions are
/// guarded by the same #if.  The functions are unused on builds that
/// don't define either macro (no caller path exists in those TUs), so
/// the conditional definition is safe.
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

/// Register built-in content compositions and built-in compositions
/// into the given registry.  Safe to call multiple times.
inline void init_compositions(CompositionRegistry& registry) {
    auto& assets = cli_asset_registry();

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
    static ExtensionCatalog content_catalog;
    // Build a minimal ExtensionContext — only compositions is used here.
    // graph_nodes, effects, assets are not needed for composition registration.
    // PR 3.5 — populate the ambient authoring registries so the CLI-wide
    // chronon3d::authoring façades can resolve `.style(id)` / `.motion(id)`
    // ambient.  Default-empty; composition packs (or test harnesses) can
    // register their own style/motion ids via the cli_*_registry() handles.
    static graph::GraphNodeCatalog             dummy_nodes;
    static effects::EffectCatalog              dummy_effects;
    static authoring::StyleRegistry&           styles  = cli_style_registry();
    static authoring::MotionRegistry&          motions = cli_motion_registry();
    ExtensionContext ctx{registry, dummy_nodes, dummy_effects, assets,
                          &styles, &motions};
    chronon3d::register_content_modules(content_catalog, ctx);
#endif
    // Register non-content built-in compositions (DarkGridBackground,
    // CameraImageClip).
    chronon3d::register_builtin_compositions(registry);
}

} // namespace chronon3d::cli
