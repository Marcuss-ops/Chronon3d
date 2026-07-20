#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/authoring/subtitle_track_builder.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/registry/text_preset_registry.hpp>

#include <doctest/doctest.h>

using namespace chronon3d;
using namespace chronon3d::presets::text;

// ═══════════════════════════════════════════════════════════════════════════
// Subtitle types and adapters
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SubtitleTrack aliases TimedTextDocument") {
    SubtitleTrack track;
    track.language = "it";
    track.title = "Demo";

    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.source_id = "c1";
    track.cues.push_back(cue);

    CHECK(track.cues.size() == 1);
    CHECK(track.cues[0].text == "Hello world");
}

TEST_CASE("subtitle_from_srt parses SRT into SubtitleTrack") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:04,000
Hello world

)";

    auto track = subtitle_from_srt(srt);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].start_s == 1.0f);
    CHECK(track.cues[0].end_s == 4.0f);
    CHECK(track.cues[0].text == "Hello world");
    CHECK(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].words[0].text == "Hello");
    CHECK(track.cues[0].words[1].text == "world");
}

TEST_CASE("subtitle_from_vtt parses WebVTT into SubtitleTrack") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:04.000
Hello world

)";

    auto track = subtitle_from_vtt(vtt);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].start_s == 1.0f);
    CHECK(track.cues[0].end_s == 4.0f);
    CHECK(track.cues[0].text == "Hello world");
}

TEST_CASE("subtitle_from_json parses JSON into SubtitleTrack") {
    const char* json = R"({
  "language": "en",
  "cues": [
    {
      "id": "c1",
      "start": 1.0,
      "end": 4.0,
      "text": "Hello world",
      "words": [
        {"word": "Hello", "start": 1.0, "end": 1.5, "id": "w1"},
        {"word": "world", "start": 2.5, "end": 4.0, "id": "w2"}
      ]
    }
  ]
})";

    auto track = subtitle_from_json(json);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.language == "en");
    CHECK(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].words[0].semantic_id == "w1");
}

// ═══════════════════════════════════════════════════════════════════════════
// Active word highlight
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("active_cue_at finds the cue covering the given time") {
    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello";
    track.cues.push_back(cue);

    CHECK(active_cue_at(track, 0.5f) == nullptr);
    CHECK(active_cue_at(track, 2.0f) != nullptr);
    CHECK(active_cue_at(track, 4.0f) == nullptr);
}

TEST_CASE("active_word_at finds the word covering the given time") {
    SubtitleCue cue;
    cue.start_s = 0.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.words = {
        TimedWord{"Hello", 0.0f, 1.5f, "w1"},
        TimedWord{"world", 2.0f, 4.0f, "w2"},
    };

    CHECK(active_word_at(cue, 0.5f) != nullptr);
    CHECK(active_word_at(cue, 0.5f)->text == "Hello");
    CHECK(active_word_at(cue, 3.0f)->text == "world");
    CHECK(active_word_at(cue, 5.0f) == nullptr);
}

TEST_CASE("active_word_style_at returns highlighted state for active word") {
    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 0.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.words = {
        TimedWord{"Hello", 0.0f, 2.0f, "w1"},
        TimedWord{"world", 2.0f, 4.0f, "w2"},
    };
    track.cues.push_back(cue);

    Color yellow{1.0f, 1.0f, 0.0f, 1.0f};
    auto state = active_word_style_at(track, 0.5f, yellow, 1.2f, std::nullopt);
    CHECK(state.highlighted);
    CHECK(state.word_index.has_value());
    CHECK(state.word_index.value() == 0u);
    CHECK(state.semantic_id == "w1");
    CHECK(state.color.has_value());
    CHECK(state.scale.value() == doctest::Approx(1.2f));

    auto inactive = active_word_style_at(track, 5.0f);
    CHECK(!inactive.highlighted);
    CHECK(!inactive.word_index.has_value());
}

// ═══════════════════════════════════════════════════════════════════════════
// Preset registry
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Subtitle preset registry contains at least 8 presets") {
    const auto& registry = chronon3d::registry::builtin_text_preset_registry();
    const auto subtitle_presets = registry.by_category(
        chronon3d::registry::TextPresetCategory::Subtitle);

    CHECK(subtitle_presets.size() >= 8);

    CHECK(registry.contains("minimal_white"));
    CHECK(registry.contains("yellow_keyword"));
    CHECK(registry.contains("glow_pulse"));
    CHECK(registry.contains("caption_box"));
    CHECK(registry.contains("karaoke_fill"));
    CHECK(registry.contains("active_word_pop"));
    CHECK(registry.contains("subtitle_card"));
    CHECK(registry.contains("lower_third_safe"));
}

TEST_CASE("New subtitle presets have Subtitle category and valid fixtures") {
    const auto& registry = chronon3d::registry::builtin_text_preset_registry();

    for (const auto& id : {"karaoke_fill", "active_word_pop", "subtitle_card", "lower_third_safe"}) {
        const auto& preset = registry.get(id);
        CHECK(preset.metadata.category == chronon3d::registry::TextPresetCategory::Subtitle);
        CHECK(!preset.fixture.empty());
        CHECK(preset.builder != nullptr);
        CHECK(preset.animator_factory != nullptr);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Frame-rate conversion and [start, end) semantics
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SubtitleTrackBuilder respects composition frame rate for cue timing") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{60, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello";
    track.cues.push_back(cue);

    layer.subtitles(track).build();

    Layer built = lb.build();
    CHECK(built.from == Frame{60});
    CHECK(built.duration == Frame{180});
}

TEST_CASE("SubtitleTrackBuilder supports standard and fractional frame rates") {
    const std::vector<FrameRate> rates = {
        FrameRate{24000, 1001},
        FrameRate{24, 1},
        FrameRate{25, 1},
        FrameRate{30000, 1001},
        FrameRate{30, 1},
        FrameRate{50, 1},
        FrameRate{60, 1},
    };

    for (const auto& rate : rates) {
        LayerBuilder lb{std::string{"test_layer_"} + std::to_string(rate.numerator),
                        SampleTime::from_frame_int(Frame{0}, rate)};
        lb.screen_dimensions(1920.0f, 1080.0f);
        CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
        chronon3d::authoring::Layer layer{lb, canvas};

        SubtitleTrack track;
        SubtitleCue cue;
        cue.start_s = 1.0f;
        cue.end_s = 4.0f;
        cue.text = "Hello";
        track.cues.push_back(cue);

        layer.subtitles(track).build();
        Layer built = lb.build();

        const auto seconds_to_frame = [&rate](double s) {
            return static_cast<Frame>(std::lround(s * static_cast<double>(rate.numerator) /
                                                      static_cast<double>(rate.denominator)));
        };
        const Frame expected_start = seconds_to_frame(1.0);
        const Frame expected_end = seconds_to_frame(4.0);
        const Frame expected_duration = std::max(Frame{1}, expected_end - expected_start);

        CHECK(built.from == expected_start);
        CHECK(built.duration == expected_duration);
    }
}

TEST_CASE("SubtitleTrackBuilder clamps zero-duration cues to at least one frame") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 1.0f + (1.0f / 90000.0f); // less than one frame at 30 fps
    cue.text = "Hello";
    track.cues.push_back(cue);

    layer.subtitles(track).build();
    Layer built = lb.build();

    CHECK(built.from == Frame{30});
    CHECK(built.duration == Frame{1});
}

TEST_CASE("SubtitleTrackBuilder frame_rate override is used when set") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello";
    track.cues.push_back(cue);

    // Override the LayerBuilder's 30 fps with 60 fps.
    layer.subtitles(track).frame_rate(FrameRate{60, 1}).build();
    Layer built = lb.build();

    CHECK(built.from == Frame{60});
    CHECK(built.duration == Frame{180});
}

// ═══════════════════════════════════════════════════════════════════════════
// Authoring API (lightweight compile/link check)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SubtitleTrackBuilder can be instantiated and configured") {
    LayerBuilder lb{"test_layer"};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 3.0f;
    cue.text = "Hello";
    track.cues.push_back(cue);

    auto builder = layer.subtitles(track)
                       .preset("minimal_white")
                       .font("fonts/Poppins-Bold.ttf", 48.0f)
                       .color(Color::white())
                       .box({1400.0f, 200.0f})
                       .align(TextAlign::Center)
                       .place(TextPlacementKind::SafeAreaBottom);

    // Build should not throw for a valid preset and non-empty track.
    CHECK_NOTHROW(builder.build());
}
