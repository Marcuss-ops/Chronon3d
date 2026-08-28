#include <chronon3d/text/timed_text_document.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

// ═══════════════════════════════════════════════════════════════════════════
// FASE 4b — TimedTextDocument + adapter tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("TimedTextDocument — empty construction") {
    TimedTextDocument doc;
    CHECK(doc.cues.empty());
    CHECK(doc.language.empty());
    CHECK(doc.title.empty());
    CHECK(doc.source_format.empty());
}

TEST_CASE("TimedTextDocument — single cue roundtrip") {
    TimedTextDocument doc;
    doc.language = "en";
    doc.title = "Test Subtitles";
    doc.source_format = "json";

    TimedCue cue;
    cue.speaker = "Narrator";
    cue.start_s = 1.0f;
    cue.end_s = 4.0f;
    cue.text = "Hello world";
    cue.source_id = "c1";

    TimedWord w1{"Hello", 1.0f, 1.5f, "c1-w0"};
    TimedWord w2{"world", 2.5f, 4.0f, "c1-w1"};
    cue.words = {w1, w2};

    doc.cues.push_back(cue);

    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].speaker == "Narrator");
    CHECK(doc.cues[0].start_s == 1.0f);
    CHECK(doc.cues[0].end_s == 4.0f);
    CHECK(doc.cues[0].text == "Hello world");
    CHECK(doc.cues[0].words.size() == 2);
    CHECK(doc.cues[0].words[0].text == "Hello");
    CHECK(doc.cues[0].words[0].semantic_id == "c1-w0");
    CHECK(doc.cues[0].words[1].text == "world");
    CHECK(doc.cues[0].words[1].semantic_id == "c1-w1");
}

TEST_CASE("TimedTextDocument — cue with style metadata") {
    TimedCue cue;
    cue.speaker = "John";
    cue.start_s = 0.0f;
    cue.end_s = 3.0f;
    cue.text = "Hello";

    TimedCueStyle style;
    style.color = "#FF0000";
    style.font_size = 1.5f;
    style.font_weight = 700;
    cue.style = style;

    CHECK(cue.style.has_value());
    CHECK(cue.style->color == "#FF0000");
    CHECK(*cue.style->font_size == 1.5f);
    CHECK(*cue.style->font_weight == 700);
}

// ── SRT adapter tests ───────────────────────────────────────────────────

TEST_CASE("timed_text_from_srt — single cue") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:04,000
Hello world

)";

    auto doc = timed_text_from_srt(srt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.source_format == "srt");
    CHECK(doc.cues[0].start_s == 1.0f);
    CHECK(doc.cues[0].end_s == 4.0f);
    CHECK(doc.cues[0].text == "Hello world");
    CHECK(doc.cues[0].source_id == "1");
    // Auto-generated word breakdown
    CHECK(doc.cues[0].words.size() == 2);
    CHECK(doc.cues[0].words[0].text == "Hello");
    CHECK(doc.cues[0].words[1].text == "world");
}

TEST_CASE("timed_text_from_srt — multiple cues") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:03,000
First cue.

2
00:00:03,500 --> 00:00:06,000
Second cue.

)";

    auto doc = timed_text_from_srt(srt);
    REQUIRE(doc.cues.size() == 2);
    CHECK(doc.cues[0].text == "First cue.");
    CHECK(doc.cues[0].start_s == 1.0f);
    CHECK(doc.cues[0].end_s == 3.0f);
    CHECK(doc.cues[0].source_id == "1");
    CHECK(doc.cues[1].text == "Second cue.");
    CHECK(doc.cues[1].start_s == 3.5f);
    CHECK(doc.cues[1].end_s == 6.0f);
    CHECK(doc.cues[1].source_id == "2");
}

TEST_CASE("timed_text_from_srt — multi-line cue text") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:04,000
Line one
Line two

)";

    auto doc = timed_text_from_srt(srt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].text == "Line one\nLine two");
}

TEST_CASE("timed_text_from_srt — empty input") {
    auto doc = timed_text_from_srt("");
    CHECK(doc.cues.empty());
    CHECK(doc.source_format == "srt");
}

TEST_CASE("timed_text_from_srt — malformed input") {
    // Missing arrow, missing timestamps etc.
    auto doc = timed_text_from_srt("garbage\nnot a timestamp\n");
    CHECK(doc.cues.empty());
}

// ── VTT adapter tests ───────────────────────────────────────────────────

TEST_CASE("timed_text_from_vtt — basic with WEBVTT header") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:04.000
Hello world

)";

    auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.source_format == "vtt");
    CHECK(doc.cues[0].start_s == 1.0f);
    CHECK(doc.cues[0].end_s == 4.0f);
    CHECK(doc.cues[0].text == "Hello world");
}

TEST_CASE("timed_text_from_vtt — with language and title metadata") {
    const char* vtt = R"(WEBVTT
Language: en
Title: Test

00:00:01.000 --> 00:00:03.000
Test subtitle

)";

    auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.language == "en");
    CHECK(doc.title == "Test");
}

TEST_CASE("timed_text_from_vtt — with cue id") {
    const char* vtt = R"(WEBVTT

cue-1
00:00:01.000 --> 00:00:03.000
Cue with id

)";

    auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].source_id == "cue-1");
    CHECK(doc.cues[0].text == "Cue with id");
}

TEST_CASE("timed_text_from_vtt — with speaker tag") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:03.000
<v John>Hello there

)";

    auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].speaker == "John");
    // Text should be stripped of tags
    CHECK(doc.cues[0].text == "Hello there");
}

TEST_CASE("timed_text_from_vtt — bold/italic tags stripped") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:03.000
<b>Bold</b> and <i>italic</i> text

)";

    auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].text == "Bold and italic text");
}

TEST_CASE("timed_text_from_vtt — malformed timestamps are rejected") {
    const char* vtt = R"(WEBVTT

00:00:61.000 --> 00:00:04.000
Invalid timestamp

00:00:01.000 --> 00:00:01.000
Zero duration

00:00:02.000 --> 00:00:03.000
Valid cue

)";
    const auto doc = timed_text_from_vtt(vtt);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].text == "Valid cue");
}

TEST_CASE("timed_text_from_vtt — missing cue text is rejected") {
    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:03.000

)";
    CHECK(timed_text_from_vtt(vtt).cues.empty());
}

TEST_CASE("timed_text_from_vtt — empty input") {
    auto doc = timed_text_from_vtt("");
    CHECK(doc.cues.empty());
    CHECK(doc.source_format == "vtt");
}

// ── JSON adapter tests ──────────────────────────────────────────────────

TEST_CASE("timed_text_from_json — basic single cue") {
    const char* json = R"({
  "language": "en",
  "cues": [
    {
      "id": "c1",
      "speaker": "Narrator",
      "start": 1.0,
      "end": 4.0,
      "text": "Hello world"
    }
  ]
})";

    auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.source_format == "json");
    CHECK(doc.language == "en");
    CHECK(doc.cues[0].source_id == "c1");
    CHECK(doc.cues[0].speaker == "Narrator");
    CHECK(doc.cues[0].start_s == 1.0f);
    CHECK(doc.cues[0].end_s == 4.0f);
    CHECK(doc.cues[0].text == "Hello world");
}

TEST_CASE("timed_text_from_json — with per-word timing") {
    const char* json = R"({
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

    auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 1);
    REQUIRE(doc.cues[0].words.size() == 2);
    CHECK(doc.cues[0].words[0].text == "Hello");
    CHECK(doc.cues[0].words[0].start_s == 1.0f);
    CHECK(doc.cues[0].words[0].end_s == 1.5f);
    CHECK(doc.cues[0].words[0].semantic_id == "w1");
    CHECK(doc.cues[0].words[1].text == "world");
    CHECK(doc.cues[0].words[1].start_s == 2.5f);
    CHECK(doc.cues[0].words[1].end_s == 4.0f);
    CHECK(doc.cues[0].words[1].semantic_id == "w2");
}

TEST_CASE("timed_text_from_json — with style") {
    const char* json = R"({
  "cues": [
    {
      "id": "c1",
      "start": 0.0,
      "end": 3.0,
      "text": "Styled text",
      "style": {
        "color": "#FF0000",
        "font_size": 1.2,
        "font_weight": 600
      }
    }
  ]
})";

    auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 1);
    REQUIRE(doc.cues[0].style.has_value());
    CHECK(*doc.cues[0].style->color == "#FF0000");
    CHECK(*doc.cues[0].style->font_size == 1.2f);
    CHECK(*doc.cues[0].style->font_weight == 600);
}

TEST_CASE("timed_text_from_json — multiple cues") {
    const char* json = R"({
  "title": "Multi-cue test",
  "cues": [
    {"id": "c1", "start": 1.0, "end": 3.0, "text": "First"},
    {"id": "c2", "start": 3.5, "end": 6.0, "text": "Second", "speaker": "Bob"}
  ]
})";

    auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 2);
    CHECK(doc.title == "Multi-cue test");
    CHECK(doc.cues[0].source_id == "c1");
    CHECK(doc.cues[0].text == "First");
    CHECK(doc.cues[1].source_id == "c2");
    CHECK(doc.cues[1].text == "Second");
    CHECK(doc.cues[1].speaker == "Bob");
}

TEST_CASE("timed_text_from_json — malformed root and invalid cues are rejected") {
    CHECK(timed_text_from_json("not-json").cues.empty());
    const char* json = R"({"cues":[
        {"id":"bad", "start":3, "end":1, "text":"backwards"},
        {"id":"empty", "start":1, "end":2, "text":""},
        {"id":"good", "start":1, "end":2, "text":"valid"}
    ]})";
    const auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].source_id == "good");
}

TEST_CASE("timed_text_from_json — empty input") {
    auto doc = timed_text_from_json("");
    CHECK(doc.cues.empty());
    CHECK(doc.source_format == "json");
}

TEST_CASE("timed_text_from_json — auto-generated word breakdown when no words") {
    const char* json = R"({
  "cues": [
    {"id": "c1", "start": 1.0, "end": 3.0, "text": "Three simple words"}
  ]
})";

    auto doc = timed_text_from_json(json);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].words.size() == 3);
    CHECK(doc.cues[0].words[0].text == "Three");
    CHECK(doc.cues[0].words[1].text == "simple");
    CHECK(doc.cues[0].words[2].text == "words");
}

// ── ASS adapter tests ─────────────────────────────────────────────────

// The canonical PipelineGen-generated ASS shape (CompileASSContent):
// [Script Info] + [V4+ Styles] + [Events] with H:MM:SS.cc Dialogue rows.
TEST_CASE("timed_text_from_ass — canonical PipelineGen single cue") {
    const char* ass = R"([Script Info]
Title: PipelineGen Auto Subtitles
ScriptType: v4.00+
WrapStyle: 0
PlayResX: 1920
PlayResY: 1080

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: vidrush-default,Arial,56,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,3,2,2,10,10,24,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.20,0:00:03.00,vidrush-default,,0,0,0,,Hello world
)";

    auto doc = timed_text_from_ass(ass);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.source_format == "ass");
    CHECK(doc.cues[0].start_s == doctest::Approx(1.2f));
    CHECK(doc.cues[0].end_s == doctest::Approx(3.0f));
    CHECK(doc.cues[0].text == "Hello world");
    CHECK(doc.cues[0].source_id == "1");
    // Auto-generated uniform-split word breakdown (SRT parity).
    CHECK(doc.cues[0].words.size() == 2);
    CHECK(doc.cues[0].words[0].text == "Hello");
    CHECK(doc.cues[0].words[1].text == "world");
    CHECK(doc.cues[0].word_timing_quality == WordTimingQuality::Estimated);
}

TEST_CASE("timed_text_from_ass — multiple cues with comma-containing text") {
    const char* ass = R"([Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.20,0:00:03.00,vidrush-default,,0,0,0,,First cue, with comma
Dialogue: 0,0:00:03.50,0:00:06.00,vidrush-default,,0,0,0,,Second cue
)";

    auto doc = timed_text_from_ass(ass);
    REQUIRE(doc.cues.size() == 2);
    CHECK(doc.cues[0].text == "First cue, with comma");
    CHECK(doc.cues[0].start_s == doctest::Approx(1.2f));
    CHECK(doc.cues[0].end_s == doctest::Approx(3.0f));
    CHECK(doc.cues[0].source_id == "1");
    CHECK(doc.cues[1].text == "Second cue");
    CHECK(doc.cues[1].start_s == doctest::Approx(3.5f));
    CHECK(doc.cues[1].end_s == doctest::Approx(6.0f));
    CHECK(doc.cues[1].source_id == "2");
}

TEST_CASE("timed_text_from_ass — \\N line breaks and \\h hard space") {
    const char* ass = R"([Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.20,0:00:04.00,vidrush-default,,0,0,0,,Line one\NLine two\hwith space
)";

    auto doc = timed_text_from_ass(ass);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].text == "Line one\nLine two with space");
}

TEST_CASE("timed_text_from_ass — reordered Format line is honored") {
    const char* ass = R"([Events]
Format: Layer, Text, End, Start, Style, Name, MarginL, MarginR, MarginV, Effect
Dialogue: 0,Reordered fields,0:00:03.00,0:00:01.20,vidrush-default,,0,0,0,,
)";

    auto doc = timed_text_from_ass(ass);
    REQUIRE(doc.cues.size() == 1);
    CHECK(doc.cues[0].text == "Reordered fields");
    CHECK(doc.cues[0].start_s == doctest::Approx(1.2f));
    CHECK(doc.cues[0].end_s == doctest::Approx(3.0f));
}

TEST_CASE("timed_text_from_ass — empty input") {
    auto doc = timed_text_from_ass("");
    CHECK(doc.cues.empty());
    CHECK(doc.source_format == "ass");
}

TEST_CASE("timed_text_from_ass — malformed input") {
    // No [Events] section, no Dialogue rows, unparseable timestamps.
    const char* ass = R"([Script Info]
Title: Nothing here

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,garbage,garbage,vidrush-default,,0,0,0,,ignored
)";
    auto doc = timed_text_from_ass(ass);
    CHECK(doc.cues.empty());
}

// ── Canonical IR equivalence across formats ─────────────────────────────
//
// The three line/serialized subtitle formats below describe the SAME
// semantic cue ("Hello world", 1.0s → 4.0s, uniform-split Estimated words).
// After parsing, the canonical TimedTextDocument CUE CORE must be
// identical regardless of source format — only format-identity fields
// (source_id, source_format) may differ. This validates that each adapter
// owns only its format syntax and converges on the shared canonical IR.

namespace {

// Compare everything EXCEPT format-identity fields (source_id,
// source_format), which legitimately differ per format (SRT index vs VTT
// id vs JSON id). We compare field-by-field instead of relying on the
// defaulted TimedCue::operator== because that defaulted comparison drags
// std::optional<TimedCueStyle> through GCC 11's synthesized operator==,
// which does not instantiate cleanly.
bool same_cue_core(const TimedCue& a, const TimedCue& b) {
    if (a.speaker != b.speaker || a.start_s != b.start_s || a.end_s != b.end_s ||
        a.text != b.text || a.word_timing_quality != b.word_timing_quality)
        return false;
    if (a.words.size() != b.words.size()) return false;
    for (std::size_t i = 0; i < a.words.size(); ++i) {
        const auto& wa = a.words[i];
        const auto& wb = b.words[i];
        // Exclude semantic_id: it is derived from the format-identity
        // source_id ("1-wordN" vs "c1-wordN"), so it legitimately differs
        // across formats. Everything else is canonical content.
        if (wa.text != wb.text || wa.start_s != wb.start_s ||
            wa.end_s != wb.end_s ||
            wa.byte_start != wb.byte_start || wa.byte_end != wb.byte_end)
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("timed_text equivalence — SRT/VTT/JSON same cue → same canonical IR") {
    const char* srt = R"(1
00:00:01,000 --> 00:00:04,000
Hello world

)";

    const char* vtt = R"(WEBVTT

00:00:01.000 --> 00:00:04.000
Hello world

)";

    const char* json = R"({
  "cues": [
    {"id": "c1", "start": 1.0, "end": 4.0, "text": "Hello world"}
  ]
})";

    auto srt_doc = timed_text_from_srt(srt);
    auto vtt_doc = timed_text_from_vtt(vtt);
    auto json_doc = timed_text_from_json(json);

    REQUIRE(srt_doc.cues.size() == 1);
    REQUIRE(vtt_doc.cues.size() == 1);
    REQUIRE(json_doc.cues.size() == 1);

    // The three formats converge on the same semantic cue core.
    REQUIRE(same_cue_core(srt_doc.cues[0], vtt_doc.cues[0]));
    REQUIRE(same_cue_core(json_doc.cues[0], vtt_doc.cues[0]));

    // Sanity: the canonical core really carries the word breakdown generated
    // by the SHARED uniform-timing pipeline (not just empty cues).
    CHECK(srt_doc.cues[0].words.size() == 2);
    CHECK(srt_doc.cues[0].words[0].text == "Hello");
    CHECK(srt_doc.cues[0].words[1].text == "world");
    CHECK(srt_doc.cues[0].word_timing_quality == WordTimingQuality::Estimated);
}

// ── Hash tests ──────────────────────────────────────────────────────────

TEST_CASE("hash_timed_text_document — identical docs have same hash") {
    TimedTextDocument a;
    a.source_format = "srt";
    TimedCue c;
    c.text = "Hello";
    c.start_s = 1.0f;
    c.end_s = 2.0f;
    c.source_id = "1";
    a.cues.push_back(c);

    TimedTextDocument b = a; // copy
    CHECK(hash_timed_text_document(a) == hash_timed_text_document(b));
}

TEST_CASE("hash_timed_text_document — different text has different hash") {
    TimedTextDocument a;
    TimedCue ca;
    ca.text = "Hello";
    ca.start_s = 1.0f;
    ca.end_s = 2.0f;
    a.cues.push_back(ca);

    TimedTextDocument b;
    TimedCue cb;
    cb.text = "World";  // different text
    cb.start_s = 1.0f;
    cb.end_s = 2.0f;
    b.cues.push_back(cb);

    CHECK(hash_timed_text_document(a) != hash_timed_text_document(b));
}

TEST_CASE("hash_timed_text_document — different timing has different hash") {
    TimedTextDocument a;
    TimedCue ca;
    ca.text = "Hello";
    ca.start_s = 1.0f;
    ca.end_s = 2.0f;
    a.cues.push_back(ca);

    TimedTextDocument b;
    TimedCue cb;
    cb.text = "Hello";
    cb.start_s = 3.0f;  // different start
    cb.end_s = 4.0f;    // different end
    b.cues.push_back(cb);

    CHECK(hash_timed_text_document(a) != hash_timed_text_document(b));
}
