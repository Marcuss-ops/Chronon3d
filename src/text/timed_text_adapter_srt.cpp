#include <chronon3d/text/timed_text_document.hpp>

#include "timed_text_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace chronon3d {
namespace {

// Parse "HH:MM:SS,mmm" or "HH:MM:SS.mmm" to seconds.
// Returns negative on parse failure.
//
// NOTE: SRT word-splitting/timing rides the shared canonical
// textcore::attach_uniform_words core (skip_tags=false, include_cr=true),
// byte-compatible with the historical inline split_words() + uniform
// distribution it replaced. See timed_text_core.hpp.
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

} // namespace

TimedTextDocument timed_text_from_srt(const std::string& raw) {
    TimedTextDocument doc;
    doc.source_format = "srt";

    if (raw.empty()) return doc;

    const std::string_view content(raw);

    // Shared canonical line split (CRLF-tolerant), see timed_text_core.hpp.
    const std::vector<std::string_view> lines = textcore::split_lines(content);

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

        // Word breakdown: shared uniform-timing canonical core. Byte offsets
        // and timing are byte-compatible with the historical inline
        // split_words() + per-word uniform distribution. SRT carries
        // cue-level timing only, so provenance is Estimated.
        textcore::attach_uniform_words(cue, cue_text,
            /*skip_angle_tags*/false, /*include_cr*/true);

        doc.cues.push_back(std::move(cue));
    }

    textcore::canonicalize_document(doc);
    return doc;
}

} // namespace chronon3d
