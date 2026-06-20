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
#endif

namespace chronon3d::cli {

/// Returns the CLI-wide static AssetRegistry.  Created once, shared
/// between init_compositions() and render_job_setup().
inline AssetRegistry& cli_asset_registry() {
    static AssetRegistry reg;
    return reg;
}

/// Register built-in content compositions and built-in compositions
/// into the given registry.  Safe to call multiple times.
inline void init_compositions(CompositionRegistry& registry) {
    auto& assets = cli_asset_registry();

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
    static ExtensionCatalog content_catalog;
    // Build a minimal ExtensionContext — only compositions is used here.
    // graph_nodes, effects, assets are not needed for composition registration.
    static graph::GraphNodeCatalog dummy_nodes;
    static effects::EffectCatalog dummy_effects;
    ExtensionContext ctx{registry, dummy_nodes, dummy_effects, assets};
    chronon3d::register_content_modules(content_catalog, ctx);
#endif
    // Register non-content built-in compositions (DarkGridBackground,
    // CameraImageClip).
    chronon3d::register_builtin_compositions(registry);
}

} // namespace chronon3d::cli
