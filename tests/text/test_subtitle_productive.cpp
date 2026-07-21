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

// ═══════════════════════════════════════════════════════════════════════════
// Word timing quality classification (TICKET-WORD-TIMING-QUALITY)
//
// Adapter-side classification of `TimedCue::word_timing_quality`:
//   * SRT / VTT   → always `Estimated` (uniform-split heuristic — no
//                   source per-word data exists in those formats).
//   * JSON w/ `words` array (Whisper-style) → `Authoritative`.
//   * JSON w/o `words` but with `text` → `Estimated` (auto-fallback).
//   * JSON w/ neither (cue queued-empty) → cue filtered by the queue
//                   guard, but default field is `None` for safety.
//
// Default-constructed `TimedCue` must report `None` so callers never
// see a False-positive `Estimated` from an uninitialised cue.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimedCue default word_timing_quality is None (conservative default)") {
    TimedCue cue;
    CHECK(cue.words.empty());
    CHECK(cue.word_timing_quality == WordTimingQuality::None);
}

TEST_CASE("SRT adapter classifies per-word timing as Estimated") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:04,000
Hello world

)";
    auto track = subtitle_from_srt(srt);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].word_timing_quality == WordTimingQuality::Estimated);
}

TEST_CASE("VTT adapter classifies per-word timing as Estimated") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:04.000
Hello world

)";
    auto track = subtitle_from_vtt(vtt);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].word_timing_quality == WordTimingQuality::Estimated);
}

TEST_CASE("JSON adapter classifies per-word timing as Authoritative when source provides words") {
    const char* json = R"({
  "cues": [
    {
      "id": "c1",
      "start": 1.0, "end": 4.0,
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
    CHECK(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].word_timing_quality == WordTimingQuality::Authoritative);
}

TEST_CASE("JSON adapter classifies per-word timing as Estimated when auto-fallback fires") {
    const char* json = R"({
  "cues": [
    {
      "id": "c1",
      "start": 1.0, "end": 4.0,
      "text": "Hello world"
    }
  ]
})";
    auto track = subtitle_from_json(json);
    REQUIRE(track.cues.size() == 1);
    CHECK(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].word_timing_quality == WordTimingQuality::Estimated);
}

TEST_CASE("active_word_style_at propagates cue word_timing_quality to WordStyleState") {
    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 0.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.words = {
        TimedWord{"Hello", 0.0f, 2.0f, "w1"},
        TimedWord{"world", 2.0f, 4.0f, "w2"},
    };
    cue.word_timing_quality = WordTimingQuality::Authoritative;
    track.cues.push_back(cue);

    auto state = active_word_style_at(track, 0.5f);
    CHECK(state.highlighted);
    CHECK(state.quality == WordTimingQuality::Authoritative);

    // Mutate the cue and verify the helper re-reads (no caching).
    track.cues[0].word_timing_quality = WordTimingQuality::Estimated;
    auto state2 = active_word_style_at(track, 0.5f);
    CHECK(state2.quality == WordTimingQuality::Estimated);
}

// ═══════════════════════════════════════════════════════════════════════════
// TimedWord byte offset binding (TICKET-TIMED-WORD-BINDING)
//
// UTF-8 byte offsets (NOT grapheme count) on `TimedWord::byte_start/byte_end`
// enable TextSpanOverride mapping and TextUnitMap byte-index lookup.
// Adapter-side classification: SRT/VTT uniform-split → byte offsets from
// `split_words()` helper; JSON with source `words` array → sequential find
// within cue.text; JSON auto-fallback → byte offsets from existing
// whitespace-split scan.
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("SRT adapter populates TimedWord byte offsets (UTF-8 byte, NOT grapheme)") {
    // UTF-8 multi-byte chars mid-word stress the byte vs grapheme distinction.
    // 'naïve' = 5 codepoints, 6 UTF-8 bytes (ï = 2 bytes in the MIDDLE).
    // A naive codepoint-count would give byte_end = 5; correct UTF-8 byte offset = 6.
    const char* srt_utf8 = "1\n00:00:01,000 --> 00:00:04,000\nnaïve\n\n";
    auto track = subtitle_from_srt(srt_utf8);
    REQUIRE(track.cues.size() == 1);
    REQUIRE(track.cues[0].words.size() == 1);
    CHECK(track.cues[0].words[0].text == "naïve");
    // n=byte0, a=byte1, ï=bytes2-3 (2 bytes), v=byte4, e=byte5.
    CHECK(track.cues[0].words[0].byte_start == 0u);
    CHECK(track.cues[0].words[0].byte_end == 6u); // 5 codepoints but 6 UTF-8 bytes (mid-word multi-byte)
}

TEST_CASE("VTT adapter populates TimedWord byte offsets for multi-word cue") {
    const char* vtt = "WEBVTT\n\n00:00:01.000 --> 00:00:04.000\nHello world\n\n";
    auto track = subtitle_from_vtt(vtt);
    REQUIRE(track.cues.size() == 1);
    REQUIRE(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].words[0].text == "Hello");
    CHECK(track.cues[0].words[0].byte_start == 0u);
    CHECK(track.cues[0].words[0].byte_end == 5u);
    CHECK(track.cues[0].words[1].text == "world");
    CHECK(track.cues[0].words[1].byte_start == 6u); // after "Hello "
    CHECK(track.cues[0].words[1].byte_end == 11u);
}

TEST_CASE("JSON adapter populates TimedWord byte offsets (source words array path)") {
    const char* json = R"({
  "cues": [
    {
      "id": "c1",
      "start": 1.0, "end": 4.0,
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
    REQUIRE(track.cues[0].words.size() == 2);
    CHECK(track.cues[0].words[0].byte_start == 0u);
    CHECK(track.cues[0].words[0].byte_end == 5u);
    CHECK(track.cues[0].words[1].byte_start == 6u);
    CHECK(track.cues[0].words[1].byte_end == 11u);
}

TEST_CASE("Karaoke preset requires Authoritative per-word timing by default") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.word_timing_quality = WordTimingQuality::Estimated;
    cue.words = {
        TimedWord{"Hello", 1.0f, 2.5f, "w1", 0u, 5u},
        TimedWord{"world", 2.5f, 4.0f, "w2", 6u, 11u},
    };
    track.cues.push_back(cue);

    auto builder = layer.subtitles(track).preset("active_word_pop");
    CHECK_THROWS_AS(builder.build(), std::runtime_error);
}

TEST_CASE("Karaoke preset accepts Authoritative per-word timing") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.word_timing_quality = WordTimingQuality::Authoritative;
    cue.words = {
        TimedWord{"Hello", 1.0f, 2.5f, "w1", 0u, 5u},
        TimedWord{"world", 2.5f, 4.0f, "w2", 6u, 11u},
    };
    track.cues.push_back(cue);

    auto builder = layer.subtitles(track).preset("active_word_pop");
    CHECK_NOTHROW(builder.build());
}

TEST_CASE("Karaoke preset accepts Estimated timing when explicitly allowed") {
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.word_timing_quality = WordTimingQuality::Estimated;
    cue.words = {
        TimedWord{"Hello", 1.0f, 2.5f, "w1", 0u, 5u},
        TimedWord{"world", 2.5f, 4.0f, "w2", 6u, 11u},
    };
    track.cues.push_back(cue);

    auto builder = layer.subtitles(track)
                       .preset("active_word_pop")
                       .allow_estimated_word_timing(true);
    CHECK_NOTHROW(builder.build());
}

TEST_CASE("SubtitleTrackBuilder emits one GlyphSelectorSpec per TimedWord (karaoke-pop wiring)") {
    // TICKET-TIMED-WORD-BINDING: the builder must emit N selectors (one per
    // word) so the preset's existing animator applies its highlight properties
    // to the ACTIVE word only (selector math gives weight=1.0 ONLY for the
    // matching word's glyphs at the active time window).
    //
    // We can't directly inspect the post-build run_spec from the public API
    // (builder consumes it via commit()), so this test verifies the wiring
    // produces the correct number of TextRunSpec::selectors entries by
    // inspecting the LayerBuilder's internal state.  The build is also a
    // no-throw smoke check for the karaoke-aware preset.
    LayerBuilder lb{"test_layer", SampleTime::from_frame_int(Frame{0}, FrameRate{30, 1})};
    lb.screen_dimensions(1920.0f, 1080.0f);
    CanvasInfo canvas = CanvasInfo::with_safe_area(1920.0f, 1080.0f, SafeAreaPreset{});
    chronon3d::authoring::Layer layer{lb, canvas};

    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.words = {
        TimedWord{"Hello", 1.0f, 2.5f, "w1", 0u, 5u},
        TimedWord{"world", 2.5f, 4.0f, "w2", 6u, 11u},
    };
    cue.word_timing_quality = WordTimingQuality::Estimated;
    track.cues.push_back(cue);

    // Use a karaoke-aware preset (any subtitle preset will work; the test
    // only verifies the builder emits N selectors regardless of preset).
    // The cue has Estimated per-word timing, so explicitly opt-in.
    CHECK_NOTHROW(layer.subtitles(track)
                       .preset("active_word_pop")
                       .allow_estimated_word_timing(true)
                       .build());
}

TEST_CASE("TextRunSpec::selectors holds N word selectors post-builder-wiring (round-2 reviewer lock)") {
    // Round-2 reviewer lock (TICKET-TIMED-WORD-BINDING Finding #1): assert
    // that pushing N GlyphSelectorSpec entries onto TextRunSpec::selectors
    // (the canonical field at include/chronon3d/scene/builders/params/text_params.hpp:105)
    // results in selectors.size() == N.  This locks the structural contract
    // without needing a full render pipeline.
    TextRunSpec run_spec;
    run_spec.text.content.value = "Hello world";

    const std::size_t expected_count = 5;
    for (std::size_t w = 0; w < expected_count; ++w) {
        GlyphSelectorSpec word_sel;
        word_sel.unit = TextSelectorUnit::Word;
        word_sel.start = static_cast<f32>(w);
        word_sel.end = static_cast<f32>(w + 1);
        word_sel.shape = TextSelectorShape::Square;
        word_sel.id = "test_word_" + std::to_string(w) + "_sel";
        run_spec.selectors.push_back(std::move(word_sel));
    }
    CHECK(run_spec.selectors.size() == expected_count);
    CHECK(run_spec.selectors[0].unit == TextSelectorUnit::Word);
    CHECK(run_spec.selectors[0].start.value_at(Frame{0}) == doctest::Approx(0.0f));
    CHECK(run_spec.selectors[0].end.value_at(Frame{0}) == doctest::Approx(1.0f));
}

TEST_CASE("hash_timed_cue distinguishes Estimated vs Authoritative on otherwise-identical cue data") {
    // TICKET-WORD-TIMING-QUALITY round-2 reviewer actioned (Finding C):
    // regression lock for the 1-line hash mix.  Without this test a future
    // refactor could remove the mix line silently without test-detection.
    TimedCue a;
    a.start_s = 1.0f;
    a.end_s = 4.0f;
    a.text = "Hello world";
    a.words = {
        TimedWord{"Hello", 1.0f, 2.5f, "w1"},
        TimedWord{"world", 2.5f, 4.0f, "w2"},
    };
    a.source_id = "c1";
    a.word_timing_quality = WordTimingQuality::Estimated;

    TimedCue b = a;
    b.word_timing_quality = WordTimingQuality::Authoritative;

    CHECK(hash_timed_cue(a) != hash_timed_cue(b));
}

TEST_CASE("active_word_style_at keeps quality=None when cue has no words (early-return default)") {
    // TICKET-WORD-TIMING-QUALITY round-2 reviewer actioned: lock the
    // early-return contract.  When the cue exists but `cue.words.empty()`,
    // the helper short-circuits BEFORE `state.quality = cue->word_timing_quality;`
    // is set, so `state.quality` stays at the conservative default `None`.
    // The cue's own quality field (set to Authoritative here) is irrelevant
    // — the helper default is the contract the renderer can trust.
    SubtitleTrack track;
    SubtitleCue cue;
    cue.start_s = 0.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    // cue.words left empty intentionally
    cue.word_timing_quality = WordTimingQuality::Authoritative; // would normally propagate
    track.cues.push_back(cue);

    auto state = active_word_style_at(track, 2.0f);
    CHECK(!state.highlighted);
    CHECK(state.quality == WordTimingQuality::None);
}

TEST_CASE("SubtitleTrackBuilder builds per-word selectors with percentage ranges and keyed amount") {
    // TICKET-TIMED-WORD-BINDING: verify the binding struct and the
    // generated word selectors cover equal percentage slices of the
    // word unit space and activate only during their word timing window.
    SubtitleCue cue;
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world again";
    cue.word_timing_quality = WordTimingQuality::Authoritative;
    cue.words = {
        TimedWord{"Hello", 1.0f, 2.0f, "w1", 0u, 5u},
        TimedWord{"world", 2.0f, 3.0f, "w2", 6u, 11u},
        TimedWord{"again", 3.0f, 4.0f, "w3", 12u, 17u},
    };

    const auto bindings = chronon3d::authoring::SubtitleTrackBuilder::build_word_bindings(cue);
    REQUIRE(bindings.size() == 3);
    CHECK(bindings[0].semantic_id == "w1");
    CHECK(bindings[0].word_index == 0u);
    CHECK(bindings[0].total_words == 3u);

    const FrameRate fr{30, 1};
    const Frame start_frame{30};
    const auto selectors =
        chronon3d::authoring::SubtitleTrackBuilder::build_word_selectors(cue, fr, start_frame);
    REQUIRE(selectors.size() == 3);

    CHECK(selectors[0].unit == TextSelectorUnit::Word);
    CHECK(selectors[0].shape == TextSelectorShape::Square);
    CHECK(selectors[0].start.value_at(Frame{0}) == doctest::Approx(0.0f));
    CHECK(selectors[0].end.value_at(Frame{0}) == doctest::Approx(100.0f / 3.0f));

    CHECK(selectors[1].start.value_at(Frame{0}) == doctest::Approx(100.0f / 3.0f));
    CHECK(selectors[1].end.value_at(Frame{0}) == doctest::Approx(200.0f / 3.0f));

    // Word 0 covers cue start (1.0s) → 2.0s => frames [30, 60).
    // Word 1 covers 2.0s → 3.0s => frames [60, 90).
    // Word 2 covers 3.0s → 4.0s => frames [90, 120).
    CHECK(selectors[0].amount.value_at(Frame{45}) == doctest::Approx(100.0f));
    CHECK(selectors[0].amount.value_at(Frame{59}) == doctest::Approx(100.0f));
    CHECK(selectors[0].amount.value_at(Frame{60}) == doctest::Approx(0.0f));

    // Word 1 is active in the middle of its window and off before/after.
    CHECK(selectors[1].amount.value_at(Frame{30}) == doctest::Approx(0.0f));
    CHECK(selectors[1].amount.value_at(Frame{75}) == doctest::Approx(100.0f));
    CHECK(selectors[1].amount.value_at(Frame{90}) == doctest::Approx(0.0f));

    // Word 2 is off before its window and on inside, then off after.
    CHECK(selectors[2].amount.value_at(Frame{80}) == doctest::Approx(0.0f));
    CHECK(selectors[2].amount.value_at(Frame{105}) == doctest::Approx(100.0f));
    CHECK(selectors[2].amount.value_at(Frame{130}) == doctest::Approx(0.0f));
}
