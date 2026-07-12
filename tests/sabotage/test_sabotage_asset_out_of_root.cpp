// ============================================================================
// tests/sabotage/test_sabotage_asset_out_of_root.cpp
// ============================================================================
//
// Sabotage scenario #5: asset resolver returns a path that escapes
// the project root (e.g. `../../etc/passwd` via a relative-path
// request, or an absolute path that points outside the canonical
// `assets/` tree). This is the path-traversal rot-pattern
// (FU06 lineage per TICKET-TEXT-VISIBILITY-PIPELINE).
//
// Engine signature (partially-verified-real surface): the keyword
// `asset_resolver` / `AssetResolver` exists in 4 real surfaces
// (machine-verified via grep 2026-07-12), all in the FONT-asset
// scope (NOT a general-purpose registry):
//   include/chronon3d/text/font_engine.hpp
//   include/chronon3d/backends/text/text_render_resources.hpp
//   include/chronon3d/backends/text/text_rasterizer_utils.hpp
//   include/chronon3d/backends/software/runtime_adapter.hpp
//   include/chronon3d/backends/software/software_renderer.hpp
// The fabricated `AssetResolverRegistry::resolve(...)` claim is
// REMOVED (no such class exists). The Phase 2 fail-loud HOOK is
// PLANNED (TICKET-SABOTAGE-PRODUCTION-HOOKS) and requires FIRST
// landing an `is_within_root(path)` helper as a forward-point
// `TICKET-SABOTAGE-PATH-ROOT-HELPER` (separate from this commit
// per AGENTS.md "Fare PR piccole e mirate").
//
// Expected fail-loud path: AssetOutOfRoot Result<> error +
// the resolver MUST NOT silently fall back to a default asset
// (which would mask the security issue).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::asset_out_of_root {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — PLANNED. The production hook
// will use the verified-real `asset_resolver` family (font_scope
// confirmed + 4 sibling surfaces) PLUS a new forward-pointed
// `is_within_root(path)` helper (TICKET-SABOTAGE-PATH-ROOT-HELPER)
// to emit `AssetOutOfRoot` Result<> on path-traversal.
bool trigger_asset_out_of_root_failure() {
    return false;
}

} // namespace chaos::sabotage::asset_out_of_root

TEST_CASE(
    "sabotage/asset_out_of_root "
    "[comp=AssetResolver, layer=ResolverLayer, "
    "node=resolve_path_OUT_escapes_root_PLANNED]"
) {
    INFO("Comp=AssetResolver [verified-real font_asset scope in "
         "font_engine.hpp / text_render_resources.hpp / ...")
    INFO("Layer=ResolverLayer [PLANNED general-asset scope]")
    INFO("Node=resolve_path() returns path escaping canonical root "
         "[PLANNED TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Sabotage scenario: resolve_path() returns a path that "
         "escapes the canonical assets/ root -> AssetOutOfRoot")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS + "
         "TICKET-SABOTAGE-PATH-ROOT-HELPER")
    FAIL_CHECK(
        "sabotage/asset_out_of_root: trigger returns false -- "
        "production fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
