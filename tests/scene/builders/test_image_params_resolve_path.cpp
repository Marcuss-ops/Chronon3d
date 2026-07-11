// ──────────────────────────────────────────────────────────────────────
//  tests/scene/builders/test_image_params_resolve_path.cpp
//
//  TICKET-LAYER-IMAGE-MANIFEST-CLEAN forward-point 0g+
//
//  Helper-specific unit-test coverage for
//  `chronon3d::detail::image_params_resolve_path`.  The helper is the
//  canonical forwarding-priority resolver introduced by forward-point 0f+
//  (commit `6f784d3e`) which consolidated the 4 dispatch sites in
//  `src/scene/builders/commands/shape_commands.cpp` (LayerBuilder::image
//  + tiled_image) and `src/scene/model/render_node_factory.cpp`
//  (RenderNodeFactory::image + tiled_image) into a single source of
//  truth.
//
//  Forwarding priority (canonical, locked at forward-point 0e):
//    - `p.asset_path` non-empty → return asset_path (manifest-clean
//      field, preferred by the STEP 3 impedance closure).
//    - Else → return `p.path` (legacy deprecated field, preserved
//      for backward-compat with the ~70 pre-0e call sites).
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

// ─── Forward-declared preprocessor shim for `[[deprecated]]` suppression
//  ────────────────────────────────────────────────────────────────────
// The `path` field on `ImageParams` is `[[deprecated]]` as of forward-point
// 0e — accessing it emits a compiler warning.  Test scenarios #3 + #4
// MUST read/write this field to lock the backward-compat branch of the
// canonical forwarding priority.  Per project conventions, the warning
// is suppressed LOCALLY inside the test via portable diagnostic pragmas
// (GCC/Clang use `-Wdeprecated-declarations`, MSVC uses C4996).  The
// pragmas are wrapped in helper macros for readability + future-proofing.
#if defined(__GNUC__) || defined(__clang__)
#define CHRONON3D_DEPR_PUSH _Pragma("GCC diagnostic push") \
                            _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define CHRONON3D_DEPR_POP  _Pragma("GCC diagnostic pop")
#elif defined(_MSC_VER)
#define CHRONON3D_DEPR_PUSH __pragma(warning(push)) \
                            __pragma(warning(disable: 4996))
#define CHRONON3D_DEPR_POP  __pragma(warning(pop))
#else
#define CHRONON3D_DEPR_PUSH ((void)0)
#define CHRONON3D_DEPR_POP  ((void)0)
#endif

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

TEST_CASE("detail::image_params_resolve_path: path-only returns legacy path") {
    // `asset_path` left default-empty, `path` populated.  This is the
    // backward-compat branch — must still resolve to the legacy `path`
    // field.  The `[[deprecated]]` warning is suppressed locally so
    // this test compiles clean on the OPP's default warning level.
    CHRONON3D_DEPR_PUSH;
    ImageParams p;
    p.path = "legacy.png";
    const std::string resolved =
        detail::image_params_resolve_path(p);
    CHRONON3D_DEPR_POP;

    CHECK(resolved == "legacy.png");
    CHECK(resolved.size() == 10u);
}

TEST_CASE("detail::image_params_resolve_path: both-set returns asset_path (priority)") {
    // Both fields populated.  This is the canonical forward-point 0e
    // invariant: asset_path wins over path.  Must NOT be order-
    // dependent (the helper uses `!p.asset_path.empty()` BEFORE
    // touching `p.path`); only the `path` access needs the
    // [[deprecated]] suppression (the `path` field is read by the
    // helper body in the L falsy-branch, suppressed here for clarity
    // even when the asset_path branch is taken at runtime).
    CHRONON3D_DEPR_PUSH;
    ImageParams p;
    p.asset_path = "asset.png";
    p.path = "legacy.png";
    const std::string resolved =
        detail::image_params_resolve_path(p);
    CHRONON3D_DEPR_POP;

    // Forward-point 0e canonical invariant: `asset_path` wins over
    // `path` when both are populated.  `path` is still readable (the
    // [[deprecated]] pragma only suppresses warnings, not storage).
    CHECK(resolved == "asset.png");
    CHECK(p.path == "legacy.png");
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
    CHECK(resolved[79] == 'z');
}
