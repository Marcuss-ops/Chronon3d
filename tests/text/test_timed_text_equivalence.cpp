#include <chronon3d/text/timed_text_document.hpp>
#include <doctest/doctest.h>

using namespace chronon3d;

namespace {
bool same_semantics(const TimedTextDocument& a, const TimedTextDocument& b) {
    if (a.cues.size() != b.cues.size()) return false;
    for (std::size_t i = 0; i < a.cues.size(); ++i) {
        const auto& x = a.cues[i];
        const auto& y = b.cues[i];
        if (x.start_s != y.start_s || x.end_s != y.end_s || x.text != y.text ||
            x.words.size() != y.words.size() || x.word_timing_quality != y.word_timing_quality) return false;
        for (std::size_t j = 0; j < x.words.size(); ++j) {
            if (x.words[j].text != y.words[j].text || x.words[j].start_s != y.words[j].start_s ||
                x.words[j].end_s != y.words[j].end_s) return false;
        }
    }
    return true;
}
}

TEST_CASE("timed text adapters produce equivalent canonical semantics") {
    const std::string vtt = "WEBVTT\n\n00:00:01.000 --> 00:00:04.000\nHello world\n";
    const std::string ass = "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:04.00,Default,,0,0,0,,Hello world\n";
    const std::string json = R"({"cues":[{"id":"c1","start":1,"end":4,"text":"Hello world"}]})";

    const auto vtt_doc = timed_text_from_vtt(vtt);
    const auto ass_doc = timed_text_from_ass(ass);
    const auto json_doc = timed_text_from_json(json);

    REQUIRE(vtt_doc.cues.size() == 1);
    REQUIRE(ass_doc.cues.size() == 1);
    REQUIRE(json_doc.cues.size() == 1);
    CHECK(same_semantics(vtt_doc, ass_doc));
    CHECK(same_semantics(vtt_doc, json_doc));
}
