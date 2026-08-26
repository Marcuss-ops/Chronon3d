#include <chronon3d/text/timed_text_document.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

namespace chronon3d {
namespace {

// The helpers below carry an `ass_` prefix because the text module compiles
// with unity builds (all adapters share one translation unit) — anonymous-
// namespace helpers would collide with the SRT/VTT adapters' local names.

// Split an ASS Dialogue row into its comma-separated fields. ASS text is the
// LAST field and may itself contain commas, so only the first (fieldCount-1)
// commas are separators — everything after them is the text verbatim.
// Returns the fields, with `text` guaranteed to be the last entry.
std::vector<std::string_view> ass_split_fields(std::string_view row) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    // ASS event rows have exactly 10 fields (Layer..Text). Splitting on the
    // first 9 commas keeps comma-containing text intact.
    for (int split = 0; split < 9; ++split) {
        const auto comma = row.find(',', start);
        if (comma == std::string_view::npos) break;
        fields.push_back(row.substr(start, comma - start));
        start = comma + 1;
    }
    fields.push_back(row.substr(start));  // text (may contain commas)
    return fields;
}

// Parse "H:MM:SS.cc" (or "HH:MM:SS.cc") ASS timestamps to seconds.
// ASS uses centiseconds after the decimal point. Returns negative on failure.
f32 ass_parse_timestamp(std::string_view ts) {
    while (!ts.empty() && (ts.front() == ' ' || ts.front() == '\t')) ts.remove_prefix(1);
    while (!ts.empty() && (ts.back() == ' ' || ts.back() == '\t')) ts.remove_suffix(1);

    const auto colon1 = ts.find(':');
    if (colon1 == std::string_view::npos) return -1.0f;
    const auto colon2 = ts.find(':', colon1 + 1);
    if (colon2 == std::string_view::npos) return -1.0f;

    const int hh = std::atoi(std::string(ts.substr(0, colon1)).c_str());
    const int mm = std::atoi(std::string(ts.substr(colon1 + 1, colon2 - colon1 - 1)).c_str());

    const auto rest = ts.substr(colon2 + 1);
    const auto dot = rest.find('.');
    const int whole_s = dot == std::string_view::npos
        ? std::atoi(std::string(rest).c_str())
        : std::atoi(std::string(rest.substr(0, dot)).c_str());
    // Centiseconds → fractional seconds.
    const int centis = dot == std::string_view::npos
        ? 0
        : std::atoi(std::string(rest.substr(dot + 1)).c_str());

    if (hh < 0 || mm < 0 || mm >= 60 || whole_s < 0 || whole_s >= 60) return -1.0f;

    return static_cast<f32>(hh) * 3600.0f + static_cast<f32>(mm) * 60.0f
        + static_cast<f32>(whole_s) + static_cast<f32>(centis) / 100.0f;
}

// Split a string into words, preserving the original byte ranges (SRT parity).
struct ass_word_range {
    std::string_view word;
    std::size_t offset;
    std::size_t length;
};

std::vector<ass_word_range> ass_split_words(std::string_view text) {
    std::vector<ass_word_range> words;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) ++i;
        if (i >= text.size()) break;
        const std::size_t start = i;
        while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') ++i;
        words.push_back({text.substr(start, i - start), start, i - start});
    }
    return words;
}

// Resolve ASS escape sequences inside Dialogue text: \N (and \n) are line
// breaks, \h is a hard space. Any remaining backslash sequences are kept
// verbatim (the canonical PipelineGen generator emits none).
std::string ass_unescape_text(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[i + 1];
            if (next == 'N' || next == 'n') {
                out += '\n';
                ++i;
                continue;
            }
            if (next == 'h') {
                out += ' ';
                ++i;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

}  // namespace

TimedTextDocument timed_text_from_ass(const std::string& raw) {
    TimedTextDocument doc;
    doc.source_format = "ass";

    if (raw.empty()) return doc;

    // Split into lines (strip trailing \r, SRT-adapter parity).
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < raw.size()) {
        const std::size_t eol = raw.find('\n', pos);
        if (eol == std::string::npos) {
            lines.push_back(std::string_view(raw).substr(pos));
            break;
        }
        std::size_t len = eol - pos;
        if (len > 0 && raw[eol - 1] == '\r') --len;
        lines.push_back(std::string_view(raw).substr(pos, len));
        pos = eol + 1;
    }

    // Event field order defaults to the ASS standard
    // (Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect,
    // Text). A `Format:` row inside [Events] overrides it.
    int idx_layer = 0, idx_start = 1, idx_end = 2, idx_style = 3, idx_text = 9;
    bool have_format = false;

    bool in_events = false;
    int cue_index = 0;

    for (std::size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
        std::string_view line = lines[line_idx];
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
        if (line.empty() || line.front() == ';') continue;  // blank / comment

        if (line.front() == '[') {
            const auto close = line.find(']');
            const std::string section = close == std::string_view::npos
                ? std::string(line)
                : std::string(line.substr(1, close - 1));
            in_events = section == "Events";
            have_format = false;
            continue;
        }
        if (!in_events) continue;

        if (line.rfind("Format:", 0) == 0) {
            // "Format: Layer, Start, End, ..." — normalize to lowercase for
            // comparison and reset to standard defaults before remapping.
            idx_layer = 0; idx_start = 1; idx_end = 2; idx_style = 3; idx_text = 9;
            const auto fields = ass_split_fields(line.substr(7));
            for (std::size_t i = 0; i < fields.size(); ++i) {
                std::string name(fields[i]);
                std::transform(name.begin(), name.end(), name.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
                while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
                if (name == "layer") idx_layer = static_cast<int>(i);
                else if (name == "start") idx_start = static_cast<int>(i);
                else if (name == "end") idx_end = static_cast<int>(i);
                else if (name == "style") idx_style = static_cast<int>(i);
                else if (name == "text") idx_text = static_cast<int>(i);
            }
            have_format = true;
            continue;
        }

        if (line.rfind("Dialogue:", 0) != 0) continue;

        const auto fields = ass_split_fields(line.substr(9));  // strip "Dialogue:"
        if (fields.size() <= static_cast<std::size_t>(idx_text)) continue;

        const f32 start_s = ass_parse_timestamp(fields[static_cast<std::size_t>(idx_start)]);
        const f32 end_s = ass_parse_timestamp(fields[static_cast<std::size_t>(idx_end)]);
        if (start_s < 0.0f || end_s < 0.0f || end_s <= start_s) continue;

        std::string cue_text = ass_unescape_text(fields[static_cast<std::size_t>(idx_text)]);
        if (cue_text.empty()) continue;

        TimedCue cue;
        cue.start_s = start_s;
        cue.end_s = end_s;
        cue.text = cue_text;
        cue.source_id = std::to_string(++cue_index);
        if (have_format && !fields[static_cast<std::size_t>(idx_style)].empty())
            cue.speaker = std::string(fields[static_cast<std::size_t>(idx_style)]);

        // Word breakdown: uniform-split heuristic, SRT parity. ASS carries
        // cue-level timing only, so the per-word timing is Estimated.
        const auto wranges = ass_split_words(cue_text);
        const f32 cue_dur = end_s - start_s;
        const f32 word_dur = cue_dur / static_cast<f32>(std::max<std::size_t>(1, wranges.size()));
        int word_idx = 0;
        for (const auto& wr : wranges) {
            TimedWord tw;
            tw.text = std::string(wr.word);
            tw.byte_start = wr.offset;
            tw.byte_end = wr.offset + wr.length;
            tw.start_s = start_s + static_cast<f32>(word_idx) * word_dur;
            tw.end_s = start_s + static_cast<f32>(word_idx + 1) * word_dur;
            tw.semantic_id = cue.source_id + "-word" + std::to_string(word_idx);
            cue.words.push_back(std::move(tw));
            ++word_idx;
        }
        cue.word_timing_quality = wranges.empty()
            ? WordTimingQuality::None
            : WordTimingQuality::Estimated;

        doc.cues.push_back(std::move(cue));
    }

    return doc;
}

}  // namespace chronon3d
