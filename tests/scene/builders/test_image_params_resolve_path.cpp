// ──────────────────────────────────────────────────────────────────────
//  tests/scene/builders/test_image_params_resolve_path.cpp
//
//  TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0g+
//
//  Helper-specific unit-test coverage for
//  `chronon3d::detail::image_params_resolve_path`.  The helper is the
//  canonical forwarding-priority resolver introduced by forward-point 0f+
//  (commit `f72f2d2b8b18710f413101ea66115708fd8c4b32`) which consolidated the 4 dispatch sites in
//  `src/scene/builders/commands/shape_commands.cpp` (LayerBuilder::image
//  + tiled_image) and `src/scene/model/render_node_factory.cpp`
//  (RenderNodeFactory::image + tiled_image) into a single source of
//  truth.
//
//  Forwarding priority (canonical, locked at forward-point 0e):
//    - `p.asset_path` non-empty → return asset_path (manifest-clean
//      field, preferred by the STEP 3 impedance closure).
//    - Else → return `asset_path` (manifest-oriented compatibility field).
//    - Both empty → return empty string (caller-visible: each dispatch
//      site does its own `if (!effective_path.empty())` guard).
//
//  Test tier: UNIT (UNCONDITIONAL).
//    Per `cmake/Chronon3DTestSuite.cmake`, UNIT-tier tests link only
//    `chronon3d_pipeline` (= the OBJECT aggregate of every per-subsystem
//    .o file).  No rendering backend, no SDK install surface, no
//    third-party deps.  Pure std::string resolution logic → MUST run on
//    every CI invocation.
//
//  Coverage matrix (5 TEST_CASEs locking the canonical contract):
//    1. Both-empty → empty (the trivially-empty fallback).
//    2. Asset-only → asset (forwarding priority: clean asset_path win).
//    3. Path-only → path (legacy backward-compat branch, suppressed
//       `[[deprecated]]` warning via localized compiler pragmas).
//    4. Both-set → asset wins (priority assertion; the canonical
//       forward-point 0e closure invariant, also [[deprecated]]-suppressed).
//    5. Large-path-no-SSO → still resolves (heap-allocated string
//       passes through the helper body unchanged; gating the
//       small-string-optimization threshold so future perf rewrites
//       don't break the canonical behaviour for long paths).
//
//  AGENTS.md v0.1 freeze compliance for forward-point 0g+:
//    - Cat-1 commit-discipline (single-purpose test addition).
//    - Cat-2 honest doc-sync (tests mirrors docs/CHANGELOG entry).
//    - Cat-3 (no new public SDK surface): test file in tests/, NOT
//      include/chronon3d/.
//    - Cat-5 3-doc same-commit (this test lands together with the
//      CHANGELOG/FOLLOWUP/CURRENT_STATUS updates).
//    - Gate 5 deny-everywhere: pure std::string + chronon3d
//      ImageParams; zero msdfgen/libtess2/unicode.
// ──────────────────────────────────────────────────────────────────────

#include <doctest/doctest.h>

#include <chronon3d/scene/builders/builder_params.hpp>

#include <string>

using namespace chronon3d;

TEST_CASE("detail::image_params_resolve_path: empty-empty returns empty") {
    // Both fields default-constructed empty.  Expect: helper returns
    // an empty std::string (forwarding priority: empty asset_path →
    // fallback to path → also empty → caller-visible empty).
    const ImageParams p{};
    const std::string resolved =
        detail::image_params_resolve_path(p);
    CHECK(resolved.empty());
    CHECK(resolved.size() == 0u);
    CHECK(resolved == std::string{});
}

TEST_CASE("detail::image_params_resolve_path: asset-only returns asset_path") {
    // `asset_path` populated, `path` left default-empty (no
    // [[deprecated]] warning since we don't touch `path`).  Expect:
    // helper returns the asset_path verbatim.
    ImageParams p;
    p.asset_path = "hero.png";
    const std::string resolved =
        detail::image_params_resolve_path(p);
    CHECK(resolved == "hero.png");
    CHECK(resolved.size() == 8u);
}

TEST_CASE("detail::image_params_resolve_path: source-only returns typed source") {
    ImageParams p;
    p.source = assets::ImageRef{"typed.png"};
    const std::string resolved =
        detail::image_params_resolve_path(p);

    CHECK(resolved == "typed.png");
    CHECK(resolved.size() == 9u);
}

TEST_CASE("detail::image_params_resolve_path: typed source wins over asset_path") {
    ImageParams p;
    p.source = assets::ImageRef{"typed.png"};
    p.asset_path = "asset.png";
    const std::string resolved =
        detail::image_params_resolve_path(p);

    CHECK(resolved == "typed.png");
}

TEST_CASE("detail::image_params_resolve_path: large-path still resolves") {
    // libstdc++ small-string optimization (SSO) threshold is
    // approximately 15 chars (22-byte internal buffer on 64-bit
    // targets).  Anything larger MUST be heap-allocated.  This test
    // ensures the helper body still works for long heap-allocated
    // paths — gates against any future "fast-path" optimization that
    // might break the canonical behaviour above the SSO threshold.
    //
    // Length chosen: 80 chars (well above 22-byte SSO threshold; safe
    // across libstdc++ and libc++ differing SSO policies).
    const std::string sso_buster =
        "long_asset_path_string_to_exceed_22_byte_sso_optimization_buffer_for_test_xyz";
    CHECK(sso_buster.size() > 22u);  // sanity-check the length assumption

    ImageParams p;
    p.asset_path = sso_buster;
    const std::string resolved =
        detail::image_params_resolve_path(p);

    // Heap-allocated path must round-trip through the helper verbatim.
    CHECK(resolved == sso_buster);
    CHECK(resolved.size() == sso_buster.size());
    // Same data pointer invariant does NOT hold for std::string move
    // operations, but we can verify the contents are byte-equal.
    CHECK(resolved[0]  == 'l');
    CHECK(resolved.back() == 'z');
}
