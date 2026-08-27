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
#include <climits>
#include <cmath>
#include <sstream>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

struct Roi {
    int x0;
    int y0;
    int x1;
    int y1;
};

// Headline ink sits at TopCenter, ink centre ≈ (640, 136) at 64 pt.
constexpr Roi kHeadlineRoi{320, 60, 960, 240};
// Band directly below the headline ink — only the drop shadow lives here.
constexpr Roi kHeadlineShadowBand{320, 250, 960, 380};
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
            if (luma(fb.get_pixel(x, y)) < bg_luma - 0.02f) ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("OverlayCert1: steady state renders headline subtitle watermark and shadow") {
    auto renderer = test::make_renderer();
    auto comp = make_overlay_cert_1(&renderer.font_engine());

    auto fb = renderer.render(comp, Frame{30});
    REQUIRE(fb != nullptr);
    REQUIRE(fb->width() == kOverlayCertWidth);
    REQUIRE(fb->height() == kOverlayCertHeight);

    std::ostringstream diag;
    diag << "headline lit=" << count_lit(*fb, kHeadlineRoi, kOverlayCertBackground)
         << " subtitle lit=" << count_lit(*fb, kSubtitleRoi, kOverlayCertBackground)
         << " watermark lit=" << count_lit(*fb, kWatermarkRoi, kOverlayCertBackground)
         << " shadow dark=" << count_dark(*fb, kHeadlineShadowBand, kOverlayCertBackground) << "\n";
    for (int y = 0; y < kOverlayCertHeight; y += 60) {
        diag << "strip y=[" << y << "," << y + 60 << ") lit="
             << count_lit(*fb, Roi{0, y, kOverlayCertWidth, y + 60}, kOverlayCertBackground) << "\n";
    }
    for (int x = 0; x < kOverlayCertWidth; x += 80) {
        diag << "col x=[" << x << "," << x + 80 << ") lit="
             << count_lit(*fb, Roi{x, 0, x + 80, kOverlayCertHeight}, kOverlayCertBackground) << "\n";
    }
    diag << "pixel(100,360) r=" << fb->get_pixel(100, 360).r
         << " g=" << fb->get_pixel(100, 360).g << " b=" << fb->get_pixel(100, 360).b << "\n"
         << "pixel(640,136) r=" << fb->get_pixel(640, 136).r
         << " g=" << fb->get_pixel(640, 136).g << " b=" << fb->get_pixel(640, 136).b << "\n";
    {
        const Scene scene = comp.evaluate(test::make_ctx(30, kOverlayCertWidth, kOverlayCertHeight));
        for (const auto& layer : scene.layers()) {
            diag << "layer '" << layer.name << "' pos=(" << layer.transform.position.x
                 << "," << layer.transform.position.y << "," << layer.transform.position.z
                 << ") scale=(" << layer.transform.scale.x << "," << layer.transform.scale.y << ")\n";
            for (const auto& node : layer.nodes) {
                diag << "  node '" << node.name << "' type=" << static_cast<int>(node.shape.type())
                     << " pos=(" << node.world_transform.position.x << ","
                     << node.world_transform.position.y << "," << node.world_transform.position.z
                     << ") scale=(" << node.world_transform.scale.x << ","
                     << node.world_transform.scale.y << ")\n";
            }
        }
    }
    INFO(diag.str());
    // OVL-01 lite: every overlay produces ink inside its expected ROI.
    CHECK(count_lit(*fb, kHeadlineRoi, kOverlayCertBackground) > 500);
    CHECK(count_lit(*fb, kSubtitleRoi, kOverlayCertBackground) > 300);
    CHECK(count_lit(*fb, kWatermarkRoi, kOverlayCertBackground) > 2000);

    // OVL-06 lite: the headline drop shadow extends below the ink.
    CHECK(count_dark(*fb, kHeadlineShadowBand, kOverlayCertBackground) > 5);

    // Nothing bleeds into an empty region of the dark background.
    const Color bg_sample = fb->get_pixel(100, 360);
    CHECK(std::abs(bg_sample.r - kOverlayCertBackground.r) < 0.02f);
    CHECK(std::abs(bg_sample.g - kOverlayCertBackground.g) < 0.02f);
    CHECK(std::abs(bg_sample.b - kOverlayCertBackground.b) < 0.02f);
}

TEST_CASE("TMP probe2: bg + headline combination matrix") {
    struct Variant {
        const char* label;
        bool bg;
        bool shadow;
        bool transition;
    };
    const Variant variants[] = {
        {"headline only", false, false, false},
        {"bg + headline", true, false, false},
        {"bg + headline + shadow", true, true, false},
        {"bg + headline + transition", true, false, true},
        {"bg + headline + shadow + transition", true, true, true},
    };
    for (const auto& v : variants) {
        auto renderer = test::make_renderer();
        auto comp = composition(
            {.name = "probe2", .width = 1280, .height = 720,
             .frame_rate = FrameRate{30, 1}, .duration = Frame{2}},
            [&](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx);
                s.font_engine(&renderer.font_engine());
                if (v.bg) {
                    s.layer("bg", [](LayerBuilder& l) { l.fill(kOverlayCertBackground); });
                }
                s.layer("headline", [&](LayerBuilder& l) {
                    l.font_engine(&renderer.font_engine());
                    if (v.shadow) l.drop_shadow({8, 8}, Color{0, 0, 0, 0.8f}, 12.0f);
                    if (v.transition) l.transition_in(overlay_cert_fade(Frame{10}));
                    l.text("headline", TextDefinition{
                        .content = {.value = "PROBE TEXT"},
                        .style = {.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter", .font_weight = 700, .font_size = 64.0f},
                            .color = Color::white()},
                        .frame = {
                            .size = {1000.0f, 120.0f},
                            .placement = TextPlacement{TextPlacementKind::TopCenter, {0.0f, 100.0f}},
                            .anchor = TextAnchor::Center,
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                            .centering_mode = TextCenteringMode::PixelInk,
                        }
                    });
                });
                return s.build();
            });
        auto fb = renderer.render(comp, Frame{1});
        REQUIRE(fb != nullptr);
        int xs[2] = {INT_MAX, INT_MIN}, ys[2] = {INT_MAX, INT_MIN};
        int n = 0;
        for (int y = 0; y < 720; ++y) {
            for (int x = 0; x < 1280; ++x) {
                const Color c = fb->get_pixel(x, y);
                if ((c.r + c.g + c.b) > 0.5f) {
                    xs[0] = std::min(xs[0], x); xs[1] = std::max(xs[1], x);
                    ys[0] = std::min(ys[0], y); ys[1] = std::max(ys[1], y);
                    ++n;
                }
            }
        }
        MESSAGE(v.label, " ink bbox x[", xs[0], ",", xs[1], "] y[", ys[0], ",", ys[1], "] count=", n);
    }
}

TEST_CASE("TMP probe: single text placements across canvas sizes") {
    struct Probe {
        int w;
        int h;
        TextPlacementKind kind;
        Vec2 offset;
        const char* label;
    };
    const Probe probes[] = {
        {1920, 1080, TextPlacementKind::Absolute, {960, 540}, "cert-like 1920x1080 center"},
        {1920, 1080, TextPlacementKind::TopCenter, {0, 100}, "cert-like 1920x1080 topcenter"},
        {1280, 720, TextPlacementKind::Absolute, {640, 360}, "720p center abs"},
        {1280, 720, TextPlacementKind::TopCenter, {0, 100}, "720p topcenter"},
        {1280, 720, TextPlacementKind::BottomCenter, {0, -80}, "720p bottomcenter"},
    };
    for (const auto& p : probes) {
        auto renderer = test::make_renderer();
        auto comp = composition(
            {.name = "probe", .width = p.w, .height = p.h,
             .frame_rate = FrameRate{30, 1}, .duration = Frame{2}},
            [&](const FrameContext& ctx) -> Scene {
                SceneBuilder s(ctx);
                s.font_engine(&renderer.font_engine());
                s.layer("probe_layer", [&](LayerBuilder& l) {
                    l.font_engine(&renderer.font_engine());
                    l.text("probe_text", TextDefinition{
                        .content = {.value = "PROBE TEXT"},
                        .style = {.font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter", .font_weight = 700, .font_size = 64.0f},
                            .color = Color::white()},
                        .frame = {
                            .size = {1000.0f, 120.0f},
                            .placement = TextPlacement{p.kind, p.offset},
                            .anchor = TextAnchor::Center,
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle,
                            .centering_mode = TextCenteringMode::PixelInk,
                        }
                    });
                });
                return s.build();
            });
        auto fb = renderer.render(comp, Frame{1});
        REQUIRE(fb != nullptr);
        int xs[2] = {INT_MAX, INT_MIN}, ys[2] = {INT_MAX, INT_MIN};
        int n = 0;
        for (int y = 0; y < p.h; ++y) {
            for (int x = 0; x < p.w; ++x) {
                const Color c = fb->get_pixel(x, y);
                if ((c.r + c.g + c.b) > 0.5f) {
                    xs[0] = std::min(xs[0], x); xs[1] = std::max(xs[1], x);
                    ys[0] = std::min(ys[0], y); ys[1] = std::max(ys[1], y);
                    ++n;
                }
            }
        }
        MESSAGE(p.label, " @", p.w, "x", p.h, " ink bbox x[", xs[0], ",", xs[1], "] y[", ys[0], ",", ys[1], "] count=", n);
    }
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
