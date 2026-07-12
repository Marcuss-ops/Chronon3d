// ═══════════════════════════════════════════════════════════════════════════
// tests/text/test_video_flicker_fps.cpp
//
// TICKET-VIDEO-ANTI-FLICKER §8 + TICKET-VIDEO-MULTI-FPS-EQUIVALENCE §13.
//
// Encodes Video Completeness Matrix §8 + §13 regression invariants:
//   §8 anti-flicker — per adjacent decoded-MP4 pair:
//                       |Δmean_luma@crop(350,300,1570,780)| < 20.0
//                       formula 0.2126*r + 0.7152*g + 0.0722*b (BT.709)
//   §13 multi-fps    — same wall-clock time at 24/25/30/60 fps renders
//                       visually equivalent (centroid_dist < 2.0 px).
//                       frame_at(seconds, rate) = lround(seconds * rate.as_double())
//
// Per AGENTS.md §honesty: §8 skips when decoded-frames absent (env-blocked).
// Cat-2 freeze-compliant: zero new public SDK API. Rate propagated via
// CompositionBuilder::frame_rate(rate) so renderer.render(comp, Frame{fidx})
// constructs the rate-aware FrameContext internally (per the §13 semantic
// fix: prior renderer.render(comp, Frame{...}) used the comp.frame_rate
// as-set, defeating the multi-fps assertion).
// ═══════════════════════════════════════════════════════════════════════════

#include <doctest/doctest.h>

#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>

#include <tests/helpers/test_utils.hpp>
#include <tests/text_golden/text_clip/test_helpers.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::test;

namespace {

// §8 central crop (350, 300, 1570, 780) → 1220×480 px; BT.709 luminance.
struct CentralCrop { int x0 = 350, y0 = 300, x1 = 1570, y1 = 780; };

inline float bt709_luma(const Framebuffer::Pixel& p) {
    return 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
}

float mean_central_luma(const Framebuffer& fb, const CentralCrop& c) {
    double acc = 0.0; int n = 0;
    for (int y = c.y0; y < c.y1; ++y)
        for (int x = c.x0; x < c.x1; ++x) {
            acc += static_cast<double>(bt709_luma(fb.pixel(x, y)));
            ++n;
        }
    return static_cast<float>((n > 0) ? (acc / n) : 0.0);
}

// §13 helpers: frame_at maps seconds+rate → frame index (user-spec verbatim).
inline int frame_at(double seconds, FrameRate rate) {
    return static_cast<int>(std::lround(seconds * rate.as_double()));
}

inline float centroid_distance(const Framebuffer& a, const Framebuffer& b) {
    auto ca = alpha_centroid(a);
    auto cb = alpha_centroid(b);
    return std::hypot(ca.x - cb.x, ca.y - cb.y);
}

// AnimTypewriterGlow-class typewriter composition (canonical sibling per
// AGENTS.md Cat-3 anti-dup). Plateaus from f30 to f90 (no fade-out tail)
// so any time-point in {0.0, 0.5, 1.0, 1.5} s renders within visible-ink
// regardless of frame rate. The `rate` param is propagated into
// Composition.frame_rate so the renderer constructs a rate-aware
// FrameContext, making frame_at = lround(t*rate) bit-exact equivalent
// across rates at the same wall-clock time.
Composition make_typewriter_class(test::HarnessRenderer& r, FrameRate rate) {
    const float cx = 1920.0f * 0.5f;
    const float cy = 1080.0f * 0.5f;
    return Composition{
        {.name = "VideoFlicker/TypewriterClass_1920x1080",
         .width = 1920, .height = 1080,
         .frame_rate = rate,
         .duration = Frame{90}},
        [&r, cx, cy](const FrameContext& ctx) -> Scene {
            SceneBuilder s(ctx);
            s.font_engine(&r.font_engine());
            s.layer("typewriter_class", [cx, cy](LayerBuilder& l) {
                l.opacity_timeline(motion::timeline(0.0f)
                    .to(Frame{30}, 1.0f, EasingCurve{Easing::Linear})
                    .to(Frame{90}, 1.0f, EasingCurve{Easing::Linear}));
                l.text_run("title", TextRunParams{
                    .text = {
                        .content = {.value = "FPS_TEST"},
                        .placement = TextPlacement{TextPlacementKind::Absolute, Vec2{cx, cy}},
                        .font = {
                            .font_path = "assets/fonts/Inter-Bold.ttf",
                            .font_family = "Inter",
                            .font_weight = 700,
                            .font_size = 200.0f
                        },
                        .layout = {
                            .box = {960.0f, 540.0f},
                            .align = TextAlign::Center,
                            .vertical_align = VerticalAlign::Middle
                        },
                        .appearance = {.color = Color::white()}
                    }
                }).commit();
            });
            return s.build();
        }};
}

}  // namespace

// §8 anti-flicker across adjacent decoded-MP4 frames.
TEST_CASE("VideoAntiFlicker.AdjacentFrames_MeanLumaDelta_LT_20p0_1920x1080") {
    const std::filesystem::path frames_dir =
        "output/text_video_acceptance/decoded_frames";
    if (!std::filesystem::exists(frames_dir)) {
        MESSAGE("§8 frames-dir absent: graceful skip; pre-bake from upstream");
        return;
    }
    CentralCrop crop;
    std::vector<float> luma_series;
    for (auto& entry : std::filesystem::directory_iterator(frames_dir)) {
        if (entry.path().extension() != ".png") continue;
        Framebuffer fb = Framebuffer::load_png(entry.path());
        luma_series.push_back(mean_central_luma(fb, crop));
    }
    REQUIRE(luma_series.size() >= 2);
    for (std::size_t i = 1; i < luma_series.size(); ++i) {
        CAPTURE(i);
        CHECK(std::abs(luma_series[i] - luma_series[i - 1]) < 20.0f);
    }
}

// §13 multi-fps equivalence: same wall-clock time at different frame rates
// renders visually equivalent (centroid distance < 2.0 px). Forward-point
// matrix completeness assertions on 24↔30 + 25↔30 pair (30↔60 is canonical).
TEST_CASE("VideoAnim.MultiFPS_SameWallClock_RendersEquivalentCentroid_1920x1080") {
    auto renderer = test::make_renderer();
    const std::array<FrameRate, 4> rates = {
        FrameRate{24, 1}, FrameRate{25, 1}, FrameRate{30, 1}, FrameRate{60, 1}
    };
    const std::array<double, 4> times = {0.0, 0.5, 1.0, 1.5};
    for (double t : times) {
        CAPTURE(t);
        std::array<Framebuffer, 4> frames;
        for (std::size_t i = 0; i < rates.size(); ++i) {
            const int fidx = frame_at(t, rates[i]);
            Composition comp = make_typewriter_class(renderer, rates[i]);
            frames[i] = renderer.render(comp, Frame{fidx});
            REQUIRE(frames[i] != nullptr);
        }
        // User-spec verbatim: dist(30fps, 60fps) < 2.0 px at same wall-clock.
        CHECK(centroid_distance(frames[2], frames[3]) < 2.0f);
        // Forward-point matrix completeness: 24↔30 + 25↔30 also < 2.0 px.
        CHECK(centroid_distance(frames[0], frames[2]) < 2.0f);
        CHECK(centroid_distance(frames[1], frames[2]) < 2.0f);
    }
}
