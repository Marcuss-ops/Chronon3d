#include <doctest/doctest.h>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/extension/extension_module.hpp>

#include <algorithm>
#include <set>
#include <string>

#if defined(CHRONON3D_HAS_CONTENT_MINIMALIST) || defined(CHRONON3D_HAS_CONTENT_TEXT) || defined(CHRONON3D_HAS_CONTENT_2D5)
#include <content/register_content_modules.hpp>
#endif

using namespace chronon3d;

// ── 2.5D Module Contract ─────────────────────────────────────────────────────

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5 module: registers with stable id") {
    register_content_modules();
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.has_module("2d5"));
}

TEST_CASE("2D5 module: idempotent registration") {
    auto& reg = ExtensionRegistry::instance();
    auto before = reg.module_count();
    register_content_modules();
    register_content_modules();
    CHECK(reg.module_count() == before);
}

TEST_CASE("2D5 module: core 2.5D scenes are available") {
    register_content_modules();
    CompositionRegistry registry;

    CHECK(registry.contains("ParallaxSimple"));
    CHECK(registry.contains("DepthScene"));
    CHECK(registry.contains("CardFlip"));
    CHECK(registry.contains("DofShowcase"));
}

TEST_CASE("2D5 module: camera test compositions are available") {
    register_content_modules();
    CompositionRegistry registry;

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

#endif // CHRONON3D_HAS_CONTENT_2D5

// ── Cross-module Contract ────────────────────────────────────────────────────

TEST_CASE("All content modules: CompositionRegistry contains no duplicates after registration") {
    CompositionRegistry registry;
    auto ids = registry.available();

    std::set<std::string> unique(ids.begin(), ids.end());
    CHECK(ids.size() == unique.size());
}

TEST_CASE("All content modules: available list is sorted (std::map guarantee)") {
    CompositionRegistry registry;
    auto ids = registry.available();

    for (size_t i = 1; i < ids.size(); ++i) {
        CHECK(ids[i - 1] <= ids[i]);
    }
}
