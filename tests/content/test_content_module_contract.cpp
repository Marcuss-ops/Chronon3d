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

// ── Minimalist Module Contract ───────────────────────────────────────────────

#ifdef CHRONON3D_HAS_CONTENT_MINIMALIST

TEST_CASE("Minimalist module: registers with stable id") {
    register_minimalist_content();
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.has_module("minimalist"));
}

TEST_CASE("Minimalist module: idempotent registration") {
    auto& reg = ExtensionRegistry::instance();
    auto before = reg.module_count();
    register_minimalist_content();
    register_minimalist_content();
    CHECK(reg.module_count() == before);
}

TEST_CASE("Minimalist module: expected text presets are available in CompositionRegistry") {
    register_minimalist_content();
    CompositionRegistry registry;

    // Core text presets
    CHECK(registry.contains("MinimalistFadeIn"));
    CHECK(registry.contains("MinimalistFadeShiftVertical"));
    CHECK(registry.contains("MinimalistFadeShiftHorizontal"));
    CHECK(registry.contains("MinimalistFocusIn"));
    CHECK(registry.contains("MinimalistRevealFromBottom"));
    CHECK(registry.contains("MinimalistCenterSplit"));
    CHECK(registry.contains("MinimalistUnderlineDraw"));
    CHECK(registry.contains("MinimalistHighlightBlock"));
    CHECK(registry.contains("MinimalistFramingBracket"));
    CHECK(registry.contains("MinimalistWordStagger"));
    CHECK(registry.contains("MinimalistScaleDrop"));
    CHECK(registry.contains("MinimalistTrackingBreathing"));
    CHECK(registry.contains("MinimalistElegantExitVertical"));
    CHECK(registry.contains("MinimalistElegantExitHorizontal"));
    CHECK(registry.contains("MinimalistCurtainClose"));
}

TEST_CASE("Minimalist module: expected image presets are available") {
    register_minimalist_content();
    CompositionRegistry registry;

    CHECK(registry.contains("MinimalistImageFadeIn"));
    CHECK(registry.contains("MinimalistImageFocusIn"));
    CHECK(registry.contains("MinimalistImageScaleDrop"));
    CHECK(registry.contains("MinimalistImageFadeShiftVertical"));
    CHECK(registry.contains("MinimalistImageCenterSplit"));
    CHECK(registry.contains("MinimalistImageRevealFromBottom"));
    CHECK(registry.contains("MinimalistImageFramingBracket"));
    CHECK(registry.contains("MinimalistImageTrackingBreathing"));
    CHECK(registry.contains("MinimalistImageElegantExit"));
    CHECK(registry.contains("MinimalistImageElasticSlide"));
}

TEST_CASE("Minimalist module: showcase compositions are available") {
    register_minimalist_content();
    CompositionRegistry registry;

    CHECK(registry.contains("MinimalistFocusQuote"));
    CHECK(registry.contains("MinimalistPhraseFloat"));
    CHECK(registry.contains("MinimalistHeroExplainer"));
    CHECK(registry.contains("MinimalistScaleExplainer"));
    CHECK(registry.contains("MinimalistCleanQuote"));
}

TEST_CASE("Minimalist module: no duplicate composition ids") {
    register_minimalist_content();
    CompositionRegistry registry;
    auto ids = registry.available();

    std::set<std::string> unique(ids.begin(), ids.end());
    // If there were duplicates, the set would be smaller
    CHECK(ids.size() == unique.size());
}

#endif // CHRONON3D_HAS_CONTENT_MINIMALIST

// ── Text Module Contract ─────────────────────────────────────────────────────

#ifdef CHRONON3D_HAS_CONTENT_TEXT

TEST_CASE("Text module: registers with stable id") {
    register_text_content();
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.has_module("text"));
}

TEST_CASE("Text module: idempotent registration") {
    auto& reg = ExtensionRegistry::instance();
    auto before = reg.module_count();
    register_text_content();
    register_text_content();
    CHECK(reg.module_count() == before);
}

TEST_CASE("Text module: core text presets are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("TextCreditRoll"));
    CHECK(registry.contains("TextSplitScreen"));
    CHECK(registry.contains("TextGlow"));
    CHECK(registry.contains("TextShadow"));
    CHECK(registry.contains("TextPulse"));
    CHECK(registry.contains("TextMultiStyle"));
    CHECK(registry.contains("TextOnBackground"));
    CHECK(registry.contains("TextGridOverlay"));
}

TEST_CASE("Text module: typewriter variants are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("TextTypewriter"));
    CHECK(registry.contains("TextTypewriterTerminal"));
    CHECK(registry.contains("TextTypewriterTerminalPreview"));
    CHECK(registry.contains("TextTypewriterQuote"));
    CHECK(registry.contains("TextTypewriterQuotePreview"));
    CHECK(registry.contains("TextTypewriterManifest"));
    CHECK(registry.contains("TextTypewriterManifestPreview"));
    CHECK(registry.contains("TextTypewriterChapter"));
    CHECK(registry.contains("TextTypewriterChapterPreview"));
    CHECK(registry.contains("TextTypewriterShowcase"));
    CHECK(registry.contains("TextTypewriterAnimationTest"));
    CHECK(registry.contains("TextTypewriterDolly"));
    CHECK(registry.contains("TextTypewriterDollyRotate"));
}

TEST_CASE("Text module: intro variants are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("TextIntroCleanReveal"));
    CHECK(registry.contains("TextIntroSweepReveal"));
    CHECK(registry.contains("TextIntroImpactPulse"));
}

TEST_CASE("Text module: premium hero compositions are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("HeroFresh"));
    CHECK(registry.contains("TextPremiumHero"));
    CHECK(registry.contains("TextPremiumHeroSaaSBlue"));
    CHECK(registry.contains("TextPremiumHeroExplainer"));
    CHECK(registry.contains("TextPremiumHeroButterySmooth"));
}

TEST_CASE("Text module: utility compositions are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("Empty"));
    CHECK(registry.contains("TextOnly"));
    CHECK(registry.contains("TextProofs"));
    CHECK(registry.contains("LilDirk"));
}

TEST_CASE("Text module: motion / demo compositions are available") {
    register_text_content();
    CompositionRegistry registry;

    CHECK(registry.contains("TextAnimatedSequence"));
    CHECK(registry.contains("TextCountdown"));
    CHECK(registry.contains("TextFadeLiftDemo"));
    CHECK(registry.contains("TextSoftDollyRevealDemo"));
    CHECK(registry.contains("TextMotionTrioDemo"));
}

#endif // CHRONON3D_HAS_CONTENT_TEXT

// ── 2.5D Module Contract ─────────────────────────────────────────────────────

#ifdef CHRONON3D_HAS_CONTENT_2D5

TEST_CASE("2D5 module: registers with stable id") {
    register_two_point_five_d_content();
    auto& reg = ExtensionRegistry::instance();
    CHECK(reg.has_module("2d5"));
}

TEST_CASE("2D5 module: idempotent registration") {
    auto& reg = ExtensionRegistry::instance();
    auto before = reg.module_count();
    register_two_point_five_d_content();
    register_two_point_five_d_content();
    CHECK(reg.module_count() == before);
}

TEST_CASE("2D5 module: core 2.5D scenes are available") {
    register_two_point_five_d_content();
    CompositionRegistry registry;

    CHECK(registry.contains("ParallaxSimple"));
    CHECK(registry.contains("DepthScene"));
    CHECK(registry.contains("CardFlip"));
    CHECK(registry.contains("DofShowcase"));
}

TEST_CASE("2D5 module: camera test compositions are available") {
    register_two_point_five_d_content();
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
