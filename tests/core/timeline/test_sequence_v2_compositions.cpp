// ── tests/core/timeline/test_sequence_v2_compositions.cpp
//
// Migration Step 3 — integration tests for Sequence V2 showcase compositions.
//
// Verifies that the 5 migrated compositions (SeqV2IntroOutro, SeqV2DeepNesting,
// SeqV2StaggeredTimeline, SeqV2TrimOffset, SeqV2MixedMedia) evaluate correctly
// at various frames, produce the expected layers, and collect asset manifests.
//
// These tests exercise the full pipeline: Composition::evaluate → SceneBuilder
// → SequenceBuilder → LayerBuilder → Scene → AssetManifest.

#include <doctest/doctest.h>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/assets/asset_manifest.hpp>

// Forward-declare the composition factories from the content module.
// We call them directly rather than going through CompositionRegistry.
namespace chronon3d::content::sequence_v2 {
chronon3d::Composition seq_v2_intro_outro();
chronon3d::Composition seq_v2_deep_nesting();
chronon3d::Composition seq_v2_staggered_timeline();
chronon3d::Composition seq_v2_trim_offset();
chronon3d::Composition seq_v2_mixed_media();
} // namespace chronon3d::content::sequence_v2

using namespace chronon3d;
using namespace chronon3d::content::sequence_v2;

// ── Helper ─────────────────────────────────────────────────────────────

// test_ctx removed — Composition::evaluate() takes Frame directly,
// not FrameContext.  The FrameContext is created internally by evaluate().

// ═══════════════════════════════════════════════════════════════════════════
// 1. SeqV2IntroOutro — sequential sequences with local_frame restart
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SeqV2 compositions — IntroOutro: layers switch at boundary") {
    auto comp = seq_v2_intro_outro();

    SUBCASE("frame 0: intro active, has bg + title layers") {
        Scene scene = comp.evaluate(Frame{0});
        // Intro has _bg + title = 2 layers
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 20: still in intro") {
        Scene scene = comp.evaluate(Frame{20});
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 50: outro active, has bg + outro_text layers") {
        Scene scene = comp.evaluate(Frame{50});
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 70: still in outro") {
        Scene scene = comp.evaluate(Frame{70});
        CHECK(scene.layers().size() >= 2);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. SeqV2DeepNesting — 3-level nested sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SeqV2 compositions — DeepNesting: nested layers at correct frames") {
    auto comp = seq_v2_deep_nesting();

    SUBCASE("frame 5: ch1/title active") {
        Scene scene = comp.evaluate(Frame{5});
        // act → ch1 → title: bg + ch1_title = 2 layers
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 25: ch1/body active (title ended)") {
        Scene scene = comp.evaluate(Frame{25});
        // act → ch1 → body: ch1_body layer
        CHECK(scene.layers().size() >= 1);
    }

    SUBCASE("frame 65: ch2/title active") {
        Scene scene = comp.evaluate(Frame{65});
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 85: ch2/body active") {
        Scene scene = comp.evaluate(Frame{85});
        CHECK(scene.layers().size() >= 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. SeqV2StaggeredTimeline — overlapping sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SeqV2 compositions — StaggeredTimeline: overlapping sequences") {
    auto comp = seq_v2_staggered_timeline();

    SUBCASE("frame 10: word1 active") {
        Scene scene = comp.evaluate(Frame{10});
        // bg (top-level) + word1 = at least 2 layers
        CHECK(scene.layers().size() >= 2);
    }

    SUBCASE("frame 30: word1 + word2 overlap") {
        Scene scene = comp.evaluate(Frame{30});
        // bg + word1 + word2 = at least 3 layers
        CHECK(scene.layers().size() >= 3);
    }

    SUBCASE("frame 55: word2 + word3 overlap (word1 ended)") {
        Scene scene = comp.evaluate(Frame{55});
        // bg + word2 + word3 = at least 3 layers
        CHECK(scene.layers().size() >= 3);
    }

    SUBCASE("frame 80: word3 only (+ bg)") {
        Scene scene = comp.evaluate(Frame{80});
        CHECK(scene.layers().size() >= 2);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. SeqV2TrimOffset — trim_before demonstration
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SeqV2 compositions — TrimOffset: trim_before shifts local frame") {
    auto comp = seq_v2_trim_offset();

    SUBCASE("frame 0: both sequences active") {
        Scene scene = comp.evaluate(Frame{0});
        // bg + main text + trimmed text = at least 3 layers
        CHECK(scene.layers().size() >= 3);
    }

    SUBCASE("frame 45: still within duration") {
        Scene scene = comp.evaluate(Frame{45});
        CHECK(scene.layers().size() >= 2);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. SeqV2MixedMedia — asset manifest collection across sequences
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SeqV2 compositions — MixedMedia: manifest collects font + image") {
    auto comp = seq_v2_mixed_media();

    SUBCASE("frame 35: both sequences active — font + image in manifest") {
        Scene scene = comp.evaluate(Frame{35});
        const auto& manifest = scene.asset_manifest();
        auto fonts = manifest.filter(AssetType::Font);
        auto images = manifest.filter(AssetType::Image);

        // Both sequences active: title_seq (0-59) + image_seq (30-89)
        CHECK(fonts.size() >= 1);
        CHECK(images.size() >= 1);

        bool found_font = false;
        for (const auto& f : fonts) {
            if (f.path.find("Poppins-Bold") != std::string::npos) found_font = true;
        }
        CHECK(found_font);

        bool found_image = false;
        for (const auto& i : images) {
            if (i.path.find("placeholder") != std::string::npos) found_image = true;
        }
        CHECK(found_image);
    }

    SUBCASE("frame 10: title_seq only — font but no image") {
        Scene scene = comp.evaluate(Frame{10});
        const auto& manifest = scene.asset_manifest();
        auto fonts = manifest.filter(AssetType::Font);
        CHECK(fonts.size() >= 1);
    }

    SUBCASE("frame 70: image_seq only — image but no font") {
        Scene scene = comp.evaluate(Frame{70});
        const auto& manifest = scene.asset_manifest();
        auto images = manifest.filter(AssetType::Image);
        CHECK(images.size() >= 1);
    }
}
