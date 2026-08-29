#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <tests/helpers/test_utils.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace chronon3d;
using namespace chronon3d::presets::text;

struct InkMetrics {
    int pixels{0};
    int min_x{0};
    int min_y{0};
    int max_x{-1};
    int max_y{-1};

    [[nodiscard]] int width() const noexcept {
        return max_x >= min_x ? max_x - min_x + 1 : 0;
    }
    [[nodiscard]] int height() const noexcept {
        return max_y >= min_y ? max_y - min_y + 1 : 0;
    }
};

std::string read_fixture(const char* name) {
    const auto path = chronon3d::test::test_repo_root() / "tests" / "fixtures" /
                      "subtitles" / name;
    std::ifstream in(path);
    REQUIRE(in.good());
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

InkMetrics ink_metrics(const Framebuffer& fb) {
    InkMetrics result;
    result.min_x = fb.width();
    result.min_y = fb.height();
    for (int y = 0; y < fb.height(); ++y) {
        for (int x = 0; x < fb.width(); ++x) {
            if (fb.get_pixel(x, y).a <= 0.05f) continue;
            ++result.pixels;
            result.min_x = std::min(result.min_x, x);
            result.min_y = std::min(result.min_y, y);
            result.max_x = std::max(result.max_x, x);
            result.max_y = std::max(result.max_y, y);
        }
    }
    return result;
}

Composition make_composition(SoftwareRenderer& renderer,
                              const SubtitleTrack& track,
                              int width,
                              int height,
                              int duration_frames,
                              const char* name) {
    return composition(
        {.name = name,
         .width = width,
         .height = height,
         .frame_rate = FrameRate{30, 1},
         .duration = duration_frames},
        [&renderer, track, width, height](const FrameContext& ctx) -> Scene {
            SceneBuilder scene(ctx);
            scene.font_engine(&renderer.font_engine());
            scene.layer("subtitle", [track, width, height](LayerBuilder& lb) {
                lb.screen_dimensions(static_cast<float>(width), static_cast<float>(height));
                CanvasInfo canvas = CanvasInfo::with_safe_area(
                    static_cast<float>(width), static_cast<float>(height), SafeAreaPreset{});
                authoring::Layer layer{lb, canvas};
                layer.subtitles(track)
                    .preset("minimal_white")
                    .font(test::bundled_font_path("assets/fonts/Poppins-Bold.ttf"), 48.0f)
                    .box({static_cast<float>(width) * 0.95f,
                          static_cast<float>(height) * 0.80f})
                    .align(TextAlign::Center)
                    .vertical_align(VerticalAlign::Middle)
                    .place(TextPlacementKind::SafeAreaBottom)
                    .allow_estimated_word_timing(true)
                    .build();
            });
            return scene.build();
        });
}

Composition make_direct_text_composition(SoftwareRenderer& renderer,
                                          int width,
                                          int height,
                                          const char* name) {
    return composition(
        {.name = name,
         .width = width,
         .height = height,
         .frame_rate = FrameRate{30, 1},
         .duration = 1},
        [&renderer, width, height](const FrameContext& ctx) -> Scene {
            SceneBuilder scene(ctx);
            scene.font_engine(&renderer.font_engine());
            scene.layer("direct_text", [&renderer, width, height](LayerBuilder& lb) {
                lb.screen_dimensions(static_cast<float>(width), static_cast<float>(height));
                lb.font_engine(&renderer.font_engine());
                TextDefinition definition;
                definition.content.value = "HELLO";
                definition.style.font.font_path =
                    test::bundled_font_path("assets/fonts/Poppins-Bold.ttf");
                definition.style.font.font_size = 48.0f;
                definition.style.color = Color::white();
                definition.frame.size = {static_cast<float>(width) * 0.95f,
                                         static_cast<float>(height) * 0.80f};
                definition.frame.placement =
                    TextPlacement{TextPlacementKind::CanvasCenter, {0.0f, 0.0f}};
                definition.frame.anchor = TextAnchor::Center;
                definition.frame.align = TextAlign::Center;
                definition.frame.vertical_align = VerticalAlign::Middle;
                lb.text("direct_hello", std::move(definition));
            });
            return scene.build();
        });
}

std::shared_ptr<Framebuffer> render_frame(SoftwareRenderer& renderer,
                                          const Composition& composition_to_render,
                                          int frame) {
    auto result = renderer.render(composition_to_render, Frame{frame});
    REQUIRE(result != nullptr);
    return result;
}

void require_visible_and_inside(const Framebuffer& fb) {
    const auto metrics = ink_metrics(fb);
    CHECK(metrics.pixels > 0);
    CHECK(metrics.min_x > 0);
    CHECK(metrics.max_x < fb.width());
    CHECK(metrics.min_y > 0);
    CHECK(metrics.max_y < fb.height());
    CHECK(metrics.width() < fb.width() * 0.95);
}

} // namespace

TEST_CASE("Sparse SRT conversation has exact 30fps cue boundaries") {
    auto renderer = chronon3d::test::make_renderer();
    const auto track = subtitle_from_srt(read_fixture("conversation_12_cues.srt"));
    REQUIRE(track.cues.size() == 12);

    const auto composition_to_render = make_composition(
        renderer, track, 1920, 1080, 1080, "SparseSrtConversation");
    for (int boundary = 90; boundary < 1080; boundary += 90) {
        CAPTURE(boundary);
        auto before = render_frame(renderer, composition_to_render, boundary - 1);
        auto after = render_frame(renderer, composition_to_render, boundary);
        require_visible_and_inside(*before);
        require_visible_and_inside(*after);
        CHECK(test::framebuffer_hash(*before) != test::framebuffer_hash(*after));
    }

    auto first = render_frame(renderer, composition_to_render, 0);
    auto middle = render_frame(renderer, composition_to_render, 45);
    auto last = render_frame(renderer, composition_to_render, 1079);
    auto after_end = render_frame(renderer, composition_to_render, 1080);
    require_visible_and_inside(*first);
    require_visible_and_inside(*middle);
    require_visible_and_inside(*last);
    CHECK(ink_metrics(*after_end).pixels == 0);
}

TEST_CASE("Sparse SRT canary renders HELLO and WORLD at exact boundaries") {
    auto renderer = chronon3d::test::make_renderer();
    SubtitleTrack track;
    track.cues.push_back(SubtitleCue{.start_s = 0.0f, .end_s = 3.0f, .text = "HELLO"});
    track.cues.push_back(SubtitleCue{.start_s = 3.0f, .end_s = 6.0f, .text = "WORLD"});

    const auto composition_to_render = make_composition(
        renderer, track, 1920, 1080, 180, "SparseSrtCanary");
    const auto frame_0 = render_frame(renderer, composition_to_render, 0);
    const auto frame_89 = render_frame(renderer, composition_to_render, 89);
    const auto frame_90 = render_frame(renderer, composition_to_render, 90);
    const auto frame_179 = render_frame(renderer, composition_to_render, 179);
    const auto frame_180 = render_frame(renderer, composition_to_render, 180);

    for (const int frame : {0, 1, 88, 89, 90, 91, 178, 179}) {
        const auto sampled = render_frame(renderer, composition_to_render, frame);
        CAPTURE(frame);
        CHECK(ink_metrics(*sampled).pixels > 0);
    }

    require_visible_and_inside(*frame_0);
    require_visible_and_inside(*frame_89);
    require_visible_and_inside(*frame_90);
    require_visible_and_inside(*frame_179);
    CHECK(test::framebuffer_hash(*frame_0) == test::framebuffer_hash(*frame_89));
    CHECK(test::framebuffer_hash(*frame_0) != test::framebuffer_hash(*frame_90));
    CHECK(test::framebuffer_hash(*frame_90) == test::framebuffer_hash(*frame_179));
    CHECK(ink_metrics(*frame_180).pixels == 0);
}

TEST_CASE("Sparse SRT canary agrees with direct text rendering") {
    auto renderer = chronon3d::test::make_renderer();
    SubtitleTrack track;
    track.cues.push_back(SubtitleCue{.start_s = 0.0f, .end_s = 3.0f, .text = "HELLO"});

    const auto subtitle = make_composition(renderer, track, 1920, 1080, 1, "SrtVsDirect");
    const auto direct = make_direct_text_composition(renderer, 1920, 1080, "DirectHello");
    const auto subtitle_frame = render_frame(renderer, subtitle, 0);
    const auto direct_frame = render_frame(renderer, direct, 0);
    CAPTURE(ink_metrics(*subtitle_frame).pixels);
    CAPTURE(ink_metrics(*direct_frame).pixels);
    CHECK(ink_metrics(*subtitle_frame).pixels > 0);
    CHECK(ink_metrics(*direct_frame).pixels > 0);
}

TEST_CASE("Sparse SRT long cues wrap inside 16:9 and 9:16 safe areas") {
    auto renderer = chronon3d::test::make_renderer();
    const auto track = subtitle_from_srt(read_fixture("wrapping_10_cues.srt"));
    REQUIRE(track.cues.size() == 10);

    const auto landscape = make_composition(
        renderer, track, 1920, 1080, 1500, "SparseSrtWrapping169");
    const auto portrait = make_composition(
        renderer, track, 1080, 1920, 1500, "SparseSrtWrapping916");

    auto landscape_frame = render_frame(renderer, landscape, 75);
    auto portrait_frame = render_frame(renderer, portrait, 75);
    require_visible_and_inside(*landscape_frame);
    require_visible_and_inside(*portrait_frame);

    const auto landscape_metrics = ink_metrics(*landscape_frame);
    const auto portrait_metrics = ink_metrics(*portrait_frame);
    CHECK(landscape_metrics.height() > 60);
    CHECK(portrait_metrics.height() > 60);
    CHECK(landscape_metrics.max_y > landscape_frame->height() / 2);
    CHECK(portrait_metrics.max_y > portrait_frame->height() / 2);
}

TEST_CASE("Sparse SRT short-long alternation does not leak cached layout") {
    auto renderer = chronon3d::test::make_renderer();
    const auto track = subtitle_from_srt(read_fixture("alternating_short_long.srt"));
    REQUIRE(track.cues.size() == 12);

    const auto composition_to_render = make_composition(
        renderer, track, 1920, 1080, 900, "SparseSrtAlternating");
    const std::array<int, 12> boundaries = {
        44, 45, 149, 150, 194, 195, 299, 300,
        344, 345, 449, 450};

    std::vector<std::uint64_t> first_pass;
    first_pass.reserve(boundaries.size());
    for (const int frame : boundaries) {
        auto rendered = render_frame(renderer, composition_to_render, frame);
        require_visible_and_inside(*rendered);
        first_pass.push_back(test::framebuffer_hash(*rendered));
    }

    for (std::size_t i = 0; i < boundaries.size(); ++i) {
        auto repeated = render_frame(renderer, composition_to_render, boundaries[i]);
        CHECK(test::framebuffer_hash(*repeated) == first_pass[i]);
    }

    const auto short_metrics = ink_metrics(*render_frame(renderer, composition_to_render, 44));
    const auto long_metrics = ink_metrics(*render_frame(renderer, composition_to_render, 45));
    CHECK(short_metrics.width() < long_metrics.width());
    CHECK(short_metrics.height() < long_metrics.height());
}
