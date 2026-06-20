#include <doctest/doctest.h>
#include <chronon3d/core/composition/composition_registry.hpp>

#include <algorithm>
#include <set>
#include <string>

using namespace chronon3d;

#include <filesystem>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
#include <chronon3d/extension/extension_catalog.hpp>
#include <chronon3d/extension/extension_context.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/assets/asset_registry.hpp>
#endif

// ── Helper: register content into the given registry ─────────────────────────
#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
static void ensure_content_registered(CompositionRegistry& registry) {
    ExtensionCatalog cat;
    graph::GraphNodeCatalog nodes;
    effects::EffectCatalog effects;
    AssetRegistry assets;
    assets.mount(std::filesystem::current_path());
    chronon3d::detail::set_default_assets_root(
        std::filesystem::current_path().string());
    ExtensionContext ctx{registry, nodes, effects, assets};
    register_content_modules(cat, ctx);
}

// Return the shared catalog for idempotency tests
static ExtensionCatalog& shared_content_catalog() {
    static ExtensionCatalog cat;
    static graph::GraphNodeCatalog nodes;
    static effects::EffectCatalog effects;
    return cat;
}
#endif


// ── 2.5D Content Contract ───────────────────────────────────────────────────

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5 content: idempotent registration") {
    CompositionRegistry registry;
    auto& cat = shared_content_catalog();
    static graph::GraphNodeCatalog nodes;
    static effects::EffectCatalog effects;
    static AssetRegistry assets;
    assets.mount(std::filesystem::current_path());
    chronon3d::detail::set_default_assets_root(
        std::filesystem::current_path().string());
    ExtensionContext ctx{registry, nodes, effects, assets};
    // register_content_modules is idempotent — subsequent calls are no-ops
    // because the catalog contains the module after the first call.
    register_content_modules(cat, ctx);
    register_content_modules(cat, ctx);
    register_content_modules(cat, ctx);
    // Duplicate entries in the registry throw, so we test the catalog guards.
    auto ids = registry.available();
    // Repeated calls to register_content_modules() must not produce duplicates.
    std::set<std::string> unique(ids.begin(), ids.end());
    CHECK(ids.size() == unique.size());
}

TEST_CASE("2D5 content: core 2.5D scenes are available") {
    CompositionRegistry registry;
    ensure_content_registered(registry);

    CHECK(registry.contains("ParallaxSimple"));
    CHECK(registry.contains("DepthScene"));
    CHECK(registry.contains("CardFlip"));
    CHECK(registry.contains("DofShowcase"));
}

#ifdef CHRONON3D_BUILD_DIAGNOSTICS
TEST_CASE("2D5 module: camera test compositions are available") {
    CompositionRegistry registry;
    ensure_content_registered(registry);

    CHECK(registry.contains("CameraOrbitTargetLockTest"));
    CHECK(registry.contains("CameraDollyPerspectiveScaleTest"));
    CHECK(registry.contains("CameraParentNullRigTest"));
    CHECK(registry.contains("CameraRollPanTiltGridTest"));
    CHECK(registry.contains("CameraSafeFramingAspectRatioTest_16_9"));
    CHECK(registry.contains("CameraSafeFramingAspectRatioTest_1_1"));
    CHECK(registry.contains("CameraSafeFramingAspectRatioTest_9_16"));
    CHECK(registry.contains("CameraSafeFramingAspectRatioTest_4_5"));
    CHECK(registry.contains("CameraFrustumCullingPrecisionTest"));
    CHECK(registry.contains("CameraKinematicJerkAndInterpolationTest"));
    CHECK(registry.contains("CameraDepthSortingStressTest"));
    CHECK(registry.contains("CameraSubpixelJitterValidationTest"));
    CHECK(registry.contains("CameraMultiTargetBoundingBoxFitTest"));
    CHECK(registry.contains("CameraDepthPerspectiveScaleDiagnosticTest"));
}
#endif // CHRONON3D_BUILD_DIAGNOSTICS

#endif // CHRONON3D_HAS_CONTENT_2D5

// ── Cross-module Contract ────────────────────────────────────────────────────

TEST_CASE("All content modules: CompositionRegistry contains no duplicates after registration") {
    CompositionRegistry registry;
    ensure_content_registered(registry);
    auto ids = registry.available();

    std::set<std::string> unique(ids.begin(), ids.end());
    CHECK(ids.size() == unique.size());
}

TEST_CASE("All content modules: available list is sorted (std::map guarantee)") {
    CompositionRegistry registry;
    ensure_content_registered(registry);
    auto ids = registry.available();

    for (size_t i = 1; i < ids.size(); ++i) {
        CHECK(ids[i - 1] <= ids[i]);
    }
}
