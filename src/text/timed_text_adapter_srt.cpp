#include <chronon3d/text/timed_text_document.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace chronon3d {
namespace {

// Parse "HH:MM:SS,mmm" or "HH:MM:SS.mmm" to seconds.
// Returns negative on parse failure.
f32 parse_srt_timestamp(std::string_view ts) {
    // Strip whitespace
    while (!ts.empty() && (ts.front() == ' ' || ts.front() == '\t')) ts.remove_prefix(1);
    while (!ts.empty() && (ts.back() == ' ' || ts.back() == '\t')) ts.remove_suffix(1);

    if (ts.size() < 8) return -1.0f;

    int hh = 0, mm = 0;
    f32 ss = 0.0f;

    auto colon1 = ts.find(':');
    if (colon1 == std::string_view::npos) return -1.0f;
    auto colon2 = ts.find(':', colon1 + 1);
    if (colon2 == std::string_view::npos) return -1.0f;

    // Hours — string_view::substr() is NOT null-terminated; copy to std::string
    hh = std::atoi(std::string(ts.substr(0, colon1)).c_str());

    // Minutes
    mm = std::atoi(std::string(ts.substr(colon1 + 1, colon2 - colon1 - 1)).c_str());

    // Seconds (may use , or . as decimal separator)
    auto ss_rest = ts.substr(colon2 + 1);
    // Normalise comma→dot in a local string for strtof
    std::string ss_str(ss_rest);
    auto sep = ss_str.find_first_of(",.");
    if (sep != std::string::npos) ss_str[sep] = '.';
    ss = std::strtof(ss_str.c_str(), nullptr);

    if (hh < 0 || mm < 0 || mm >= 60 || ss < 0.0f || ss >= 60.0f) return -1.0f;

    return static_cast<f32>(hh) * 3600.0f + static_cast<f32>(mm) * 60.0f + ss;
}

// Split a string into words, preserving the original byte ranges.
struct WordRange {
    std::string_view word;
    size_t offset;  // byte offset in the source string
    size_t length;
};

std::vector<WordRange> split_words(std::string_view text) {
    std::vector<WordRange> words;
    size_t i = 0;
    while (i < text.size()) {
        // Skip whitespace
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= text.size()) break;

        size_t start = i;
        // Consume non-whitespace
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') ++i;
        words.push_back({text.substr(start, i - start), start, i - start});
    }
    return words;
}

} // namespace

TimedTextDocument timed_text_from_srt(const std::string& raw) {
    TimedTextDocument doc;
    doc.source_format = "srt";

    if (raw.empty()) return doc;

    std::string_view content(raw);

    // Split into lines
    std::vector<std::string_view> lines;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string_view::npos) {
            lines.push_back(content.substr(pos));
            break;
        }
        // Include up to but not including \r if present
        size_t len = eol - pos;
        if (len > 0 && content[eol - 1] == '\r') --len;
        lines.push_back(content.substr(pos, len));
        pos = eol + 1;
    }

    size_t line_idx = 0;
    while (line_idx < lines.size()) {
        // Skip blank lines
        while (line_idx < lines.size() && lines[line_idx].empty()) ++line_idx;
        if (line_idx >= lines.size()) break;

        // Index line (we don't strictly validate — SRT indices can be any integer)
        std::string_view index_line = lines[line_idx++];
        if (line_idx >= lines.size()) break;

        // Timestamp line: "HH:MM:SS,mmm --> HH:MM:SS,mmm"
        std::string_view ts_line = lines[line_idx++];
        auto arrow = ts_line.find("-->");
        if (arrow == std::string_view::npos) continue;  // malformed, skip

        f32 start_s = parse_srt_timestamp(ts_line.substr(0, arrow));
        f32 end_s = parse_srt_timestamp(ts_line.substr(arrow + 3));
        if (start_s < 0.0f || end_s < 0.0f || end_s <= start_s) continue;

        // Text lines — accumulate until blank line
        std::string cue_text;
        while (line_idx < lines.size() && !lines[line_idx].empty()) {
            if (!cue_text.empty()) cue_text += '\n';
            cue_text += lines[line_idx];
            ++line_idx;
        }

        if (cue_text.empty()) continue;

        TimedCue cue;
        cue.start_s = start_s;
        cue.end_s = end_s;
        cue.text = cue_text;
        cue.source_id = std::string(index_line);

        // Generate word breakdown
        auto wranges = split_words(cue_text);
        int word_idx = 0;
        for (const auto& wr : wranges) {
            TimedWord tw;
            tw.text = std::string(wr.word);
            // TICKET-TIMED-WORD-BINDING: byte offsets come straight from
            // the split_words() helper which already captures UTF-8 byte
            // ranges in the source string.
            tw.byte_start = wr.offset;
            tw.byte_end   = wr.offset + wr.length;
            // Distribute word timings evenly across the cue duration
            f32 cue_dur = end_s - start_s;
            f32 word_dur = cue_dur / static_cast<f32>(wranges.size());
            tw.start_s = start_s + static_cast<f32>(word_idx) * word_dur;
            tw.end_s = start_s + static_cast<f32>(word_idx + 1) * word_dur;
            tw.semantic_id = cue.source_id + "-word" + std::to_string(word_idx);
            cue.words.push_back(std::move(tw));
            ++word_idx;
        }

        // TICKET-WORD-TIMING-QUALITY: SRT format carries cue-level
        // timing only; the per-word breakdown above is the uniform-split
        // heuristic, so it is `Estimated` — usable for highlight
        // animation but NEVER for source-of-truth sync.
        cue.word_timing_quality = wranges.empty()
            ? WordTimingQuality::None
            : WordTimingQuality::Estimated;

        doc.cues.push_back(std::move(cue));
    }

    return doc;
}

} // namespace chronon3d
