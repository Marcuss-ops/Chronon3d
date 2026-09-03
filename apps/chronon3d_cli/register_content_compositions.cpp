// ============================================================================
// apps/chronon3d_cli/register_content_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — content-module composition bridge.
// Always called (production CLI + DEV CLI).
//
// Bridges to chronon3d::register_content_modules() which orchestrates
// per-domain register_*_compositions() calls for the content module
// (cinematic showcases, examples, certifications, launches, text placement,
// etc.).  See content/register_content_modules.cpp for the per-domain
// registration table.
//
// Uses a local ExtensionContext: registration owns the catalogs and
// authoring registries for this composition-registration pass.
//
// AE_CAM_* are no longer registered here — they are DEV-only per the
// user-spec verbatim §3 (isola runtime/dev) and are registered in
// register_dev_compositions.cpp instead.  This removes the duplicate
// `register_ae_parity_compositions` call from register_content_modules.
// ============================================================================

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>

#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
#include <content/register_content_modules.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/assets/asset_registry.hpp>
// TICKET-BENCH-CORPUS-V1 — register the 12-scene benchmark corpus (B00-B11)
// via the canonical bridge in bench/corpus/.  Hooked here so
// the corpus is available everywhere the content module is built (same
// gate).  Follows ADR-016 single-source-of-truth — no duplicate
// registration sites anywhere.
#endif

namespace chronon3d {

void register_content_compositions(CompositionRegistry& registry) {
#if defined(CHRONON3D_BUILD_CONTENT) || defined(CHRONON3D_BUILD_DIAGNOSTICS)
    // Static ExtensionCatalog + stub graph/effects catalogs (per-domain
    // register functions only consume ctx.compositions; ctx.assets is a
    // real AssetRegistry instance but is unused by the composition
    // registration path — asset resolution happens at render time, not
    // at registration time).
    static ExtensionCatalog       content_catalog;
    static graph::GraphNodeCatalog dummy_nodes;
    static effects::EffectCatalog  dummy_effects;
    static AssetRegistry          stub_assets;
    ExtensionContext ctx{registry, dummy_nodes, dummy_effects, stub_assets};
    chronon3d::register_content_modules(content_catalog, ctx);
#endif
    // When CHRONON3D_BUILD_CONTENT=OFF and CHRONON3D_BUILD_DIAGNOSTICS=OFF,
    // no content compositions are registered (production headless CLI).
}

} // namespace chronon3d
