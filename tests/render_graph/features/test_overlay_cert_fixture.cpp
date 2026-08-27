// ═══════════════════════════════════════════════════════════════════════════
// tests/render_graph/features/test_overlay_cert_fixture.cpp
//
// Smoke validation for the canonical OVERLAY-CERT-1 fixture.
// (tests/helpers/overlay_cert_fixture.hpp).  This is NOT the full overlay
// certification suite (OVL-01..OVL-12 live in their own dedicated files) —
// it locks the fixture contract itself so every downstream lane starts from
// a verified scene:
//
//   1. steady state (frame 30) renders headline + subtitle + watermark ink
//      inside their expected ROIs, with visible shadow and a clean dark
//      background elsewhere;
//   2. the three fade-ins are real temporal animations (headline ROI energy
//      strictly grows across frames 0, 2, 5, 9 and saturates at frame 10);
//   3. the scene is deterministic and static after the last transition:
//      frame 10 ≡ frame 239 (hash-equal), frame 0 ≠ frame 10.
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/helpers/render_fixtures.hpp>
#include <tests/helpers/overlay_cert_fixture.hpp>

#include <algorithm>
#include <cmath>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

struct Roi {
    int x0;
    int y0;
    int x1;
    int y1;
};

Color overlay_cert_background_linear() {
    return kOverlayCertBackground.to_linear();
}

// Headline ink sits at TopCenter, ink centre ≈ (640, 136) at 64 pt.
constexpr Roi kHeadlineRoi{320, 60, 960, 240};
// Band directly below the headline ink — only the drop shadow lives here.
constexpr Roi kHeadlineShadowBand{300, 180, 1000, 240};
// Subtitle ink sits at BottomCenter, ink centre ≈ (640, 604) at 42 pt.
constexpr Roi kSubtitleRoi{320, 540, 960, 700};
// Watermark is pinned TopRight with 20 px margin: x ≈ [1080, 1260], y ≈ [20, 200].
constexpr Roi kWatermarkRoi{1060, 0, 1280, 220};

float roi_energy(const Framebuffer& fb, const Roi& roi, const Color& bg) {
    const float bg_luma = luma(bg);
    float energy = 0.0f;
    for (int y = roi.y0; y < roi.y1; ++y) {
        for (int x = roi.x0; x < roi.x1; ++x) {
            const float v = luma(fb.get_pixel(x, y)) - bg_luma;
            if (v > 0.0f) energy += v;
        }
    }
    return energy;
}

// Pixels clearly brighter than the background (overlay ink, AA included).
int count_lit(const Framebuffer& fb, const Roi& roi, const Color& bg) {
    const float bg_luma = luma(bg);
    int n = 0;
    for (int y = roi.y0; y < roi.y1; ++y) {
        for (int x = roi.x0; x < roi.x1; ++x) {
            if (luma(fb.get_pixel(x, y)) > bg_luma + 0.05f) ++n;
        }
    }
    return n;
}

// Pixels clearly darker than the background (black shadow over dark bg).
int count_dark(const Framebuffer& fb, const Roi& roi, const Color& bg) {
    const float bg_luma = luma(bg);
    int n = 0;
    for (int y = roi.y0; y < roi.y1; ++y) {
        for (int x = roi.x0; x < roi.x1; ++x) {
            // Framebuffers store linear-light values; on a very dark
            // background a valid black shadow can differ by only a few
            // thousandths, not by 0.02 display-referred units.
            if (luma(fb.get_pixel(x, y)) < bg_luma - 0.00001f) ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("OverlayCert1: steady state renders headline subtitle watermark and shadow") {
    auto renderer = test::make_renderer();
    auto comp = make_overlay_cert_1(&renderer.font_engine());
    const Color expected_bg = overlay_cert_background_linear();

    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == kOverlayCertWidth);
    REQUIRE(fb->height() == kOverlayCertHeight);

    // OVL-01 lite: every overlay produces ink inside its expected ROI.
    CHECK(count_lit(*fb, kHeadlineRoi, expected_bg) > 500);
    CHECK(count_lit(*fb, kSubtitleRoi, expected_bg) > 300);
    CHECK(count_lit(*fb, kWatermarkRoi, expected_bg) > 2000);

    // OVL-06 lite: the headline drop shadow extends below the ink.
    CHECK(count_dark(*fb, kHeadlineShadowBand, expected_bg) > 5);

    // Nothing bleeds into an empty region of the dark background.
    const Color bg_sample = fb->get_pixel(100, 360);
    CHECK(std::abs(bg_sample.r - expected_bg.r) < 0.02f);
    CHECK(std::abs(bg_sample.g - expected_bg.g) < 0.02f);
    CHECK(std::abs(bg_sample.b - expected_bg.b) < 0.02f);
}

TEST_CASE("OverlayCert1: headline fade-in is monotonic across 0,2,5,9,10 frames") {
    auto renderer = test::make_renderer();
    auto comp = make_overlay_cert_1(&renderer.font_engine());

    const Frame frames[] = {Frame{0}, Frame{2}, Frame{5}, Frame{9}, Frame{10}};
    float energies[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 5; ++i) {
        auto fb = renderer.render(comp, frames[i]);
        REQUIRE(fb != nullptr);
        energies[i] = roi_energy(*fb, kHeadlineRoi, kOverlayCertBackground);
        INFO("frame=", frames[i].integral(), " headline ROI energy=", energies[i]);
    }

    // Linear crossfade over 10 frames: strictly increasing energy, frame 0
    // fully transparent (nothing drawn), frame 10 at full opacity.
    CHECK(energies[0] == 0.0f);
    CHECK(energies[0] < energies[1]);
    CHECK(energies[1] < energies[2]);
    CHECK(energies[2] < energies[3]);
    CHECK(energies[3] < energies[4]);
}

TEST_CASE("OverlayCert1: deterministic and static after the last transition") {
    auto renderer = test::make_renderer();
    auto comp = make_overlay_cert_1(&renderer.font_engine());

    // All fade-ins end by frame 10 (10 / 5 / 8 frames) → frames 10..239 must
    // be pixel-identical (canonical invariant for FULL/SPARSE reuse later).
    auto fb10 = renderer.render(comp, Frame{10});
    auto fb239 = renderer.render(comp, Frame{239});
    auto fb0 = renderer.render(comp, Frame{0});
    REQUIRE(fb10 != nullptr);
    REQUIRE(fb239 != nullptr);
    REQUIRE(fb0 != nullptr);

    CHECK(framebuffer_hash(*fb10) == framebuffer_hash(*fb239));
    CHECK(framebuffer_hash(*fb0) != framebuffer_hash(*fb10));
}
