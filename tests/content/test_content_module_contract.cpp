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
#include <chronon3d/runtime/render_runtime.hpp>
#endif

// ── Helper: register content into the given registry ─────────────────────────
#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_2D5)
static void ensure_content_registered(CompositionRegistry& registry) {
    ExtensionCatalog cat;
    graph::GraphNodeCatalog nodes;
    effects::EffectCatalog effects;
    AssetRegistry assets;
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

TEST_CASE("Content type accessors return the tagged content after registration") {
    CompositionRegistry registry;
    ensure_content_registered(registry);

    // The four canonical content types resolve to their tagged compositions.
    CHECK(registry.phrases().size() == 4);
    CHECK(registry.important_words().size() == 12);
    CHECK(registry.named_texts().size() == 7);

    // Every descriptor surfaced by a typed accessor carries that type's tag.
    for (const auto& descriptor : registry.phrases()) {
        CHECK(descriptor.category == content_category::Phrase);
    }
    for (const auto& descriptor : registry.important_words()) {
        CHECK(descriptor.category == content_category::ImportantWord);
    }
    for (const auto& descriptor : registry.named_texts()) {
        CHECK(descriptor.category == content_category::NamedText);
    }

    // Known ids land in the correct accessor.
    const auto contains_id = [](const std::vector<CompositionDescriptor>& descriptors,
                                std::string_view id) {
        return std::any_of(descriptors.begin(), descriptors.end(),
                           [id](const CompositionDescriptor& d) { return d.id == id; });
    };
    CHECK(contains_id(registry.phrases(), "ImportantPhrasesStack"));
    CHECK(contains_id(registry.important_words(), "ImportantWordFocus"));
    CHECK(contains_id(registry.named_texts(), "SpecialNameFadeUp"));

    // Image compositions are DEV-gated; otherwise the accessor is empty.
#ifdef CHRONON3D_BUILD_DIAGNOSTICS
    CHECK(registry.images().size() == 8);
#else
    CHECK(registry.images().empty());
#endif
}
