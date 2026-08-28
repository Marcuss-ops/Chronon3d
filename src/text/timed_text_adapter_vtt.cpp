#include <chronon3d/text/timed_text_document.hpp>

#include "timed_text_core.hpp"
#include "timed_text_adapter_support.hpp"

#include <cstdlib>
#include <string_view>

namespace chronon3d {
namespace {

// Parse "HH:MM:SS.mmm" (VTT uses dot, not comma) to seconds.
f32 parse_vtt_timestamp(std::string_view ts) {
    while (!ts.empty() && (ts.front() == ' ' || ts.front() == '\t')) ts.remove_prefix(1);
    while (!ts.empty() && (ts.back() == ' ' || ts.back() == '\t')) ts.remove_suffix(1);

    if (ts.size() < 8) return -1.0f;

    int hh = 0, mm = 0;
    f32 ss = 0.0f;

    auto colon1 = ts.find(':');
    if (colon1 == std::string_view::npos) return -1.0f;
    auto colon2 = ts.find(':', colon1 + 1);
    if (colon2 == std::string_view::npos) return -1.0f;

    hh = std::atoi(std::string(ts.substr(0, colon1)).c_str());
    mm = std::atoi(std::string(ts.substr(colon1 + 1, colon2 - colon1 - 1)).c_str());

    // Seconds part (dot-separated)
    auto ss_rest = ts.substr(colon2 + 1);
    auto dot_pos = ss_rest.find('.');
    if (dot_pos == std::string_view::npos) {
        ss = static_cast<f32>(std::atoi(std::string(ss_rest).c_str()));
    } else {
        std::string ss_str(ss_rest);
        ss = std::strtof(ss_str.c_str(), nullptr);
    }

    if (hh < 0 || mm < 0 || mm >= 60 || ss < 0.0f || ss >= 60.0f) return -1.0f;

    return static_cast<f32>(hh) * 3600.0f + static_cast<f32>(mm) * 60.0f + ss;
}

// Strip HTML-like tags from a VTT cue text (e.g. <b>, <i>, <v Speaker>)
std::string strip_vtt_tags(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool in_tag = false;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '<') {
            in_tag = true;
            continue;
        }
        if (text[i] == '>' && in_tag) {
            in_tag = false;
            continue;
        }
        if (!in_tag) result += text[i];
    }
    return result;
}

// Extract speaker from <v Speaker> tag if present
std::string extract_vtt_speaker(const std::string& text) {
    auto v_pos = text.find("<v ");
    if (v_pos == std::string::npos) {
        v_pos = text.find("<v>");
        if (v_pos != std::string::npos) return "Speaker";
        return "";
    }

    auto end = text.find('>', v_pos);
    if (end == std::string::npos) return "";

    std::string_view tag(text.data() + v_pos + 3, end - v_pos - 3);
    // Trim trailing whitespace
    while (!tag.empty() && (tag.back() == ' ' || tag.back() == '\t')) tag.remove_suffix(1);
    return std::string(tag);
}

} // namespace

TimedTextDocument timed_text_from_vtt(const std::string& raw) {
    timed_text_adapter_detail::AdapterDocumentBuilder output("vtt");
    if (raw.empty()) return output.finish();

    const std::string_view content(raw);

    // Shared canonical line split (CRLF-tolerant), see timed_text_core.hpp.
    const std::vector<std::string_view> lines = textcore::split_lines(content);

    size_t line_idx = 0;

    // Skip WEBVTT header
    if (line_idx < lines.size()) {
        std::string_view hdr(lines[line_idx]);
        // Case-insensitive check for "WEBVTT" (shared core helper).
        if (textcore::starts_with_ci(hdr, "WEBVTT")) {
            ++line_idx;

                // Optional metadata after WEBVTT header until blank line or cue start
                while (line_idx < lines.size() && !lines[line_idx].empty()) {
                    auto& meta = lines[line_idx];
                    if (meta.find("Language:") == 0 || meta.find("language:") == 0) {
                        auto colon = meta.find(':');
                        if (colon != std::string_view::npos) {
                            output.document.language = timed_text_adapter_detail::trim_ascii(meta.substr(colon + 1));
                        }
                    } else if (meta.find("Title:") == 0 || meta.find("title:") == 0) {
                        auto colon = meta.find(':');
                        if (colon != std::string_view::npos) {
                            output.document.title = timed_text_adapter_detail::trim_ascii(meta.substr(colon + 1));
                        }
                    }
                    ++line_idx;
                }
            }
        }

    // Skip blank lines and style blocks after header
    while (line_idx < lines.size()) {
        while (line_idx < lines.size() && lines[line_idx].empty()) ++line_idx;
        if (line_idx >= lines.size()) break;

        // Skip STYLE blocks
        std::string_view ll(lines[line_idx]);
        // "STYLE<space-or-colon>" starts a VTT style block (shared core
        // helper for the case-insensitive keyword match).
        if (ll.size() > 6 && textcore::starts_with_ci(ll, "STYLE") &&
            (ll[5] == ':' || ll[5] == ' ')) {
            // Skip until blank line
            while (line_idx < lines.size() && !lines[line_idx].empty()) ++line_idx;
            continue;
        }

        // Optional cue id (non-empty, non-timestamp line before timestamp)
        std::string cue_id;
        auto& maybe_id = lines[line_idx];
        if (!maybe_id.empty() && maybe_id.find("-->") == std::string_view::npos) {
            cue_id = std::string(maybe_id);
            ++line_idx;

            // Skip NOTE comments (shared core helper for the case-insensitive
            // keyword match).
            if (textcore::starts_with_ci(cue_id, "NOTE")) {
                while (line_idx < lines.size() && !lines[line_idx].empty()) ++line_idx;
                continue;
            }
        }

        if (line_idx >= lines.size()) break;

        // Timestamp line
        std::string_view ts_line = lines[line_idx++];
        auto arrow = ts_line.find("-->");
        if (arrow == std::string_view::npos) continue;

        // VTT supports optional positioning after the arrow — trim it
        std::string_view end_part = ts_line.substr(arrow + 3);
        // Strip leading whitespace left by the "--> " separator
        // (e.g. "00:00:01.000 --> 00:00:04.000" → after arrow+3 starts with " 00:00:04.000").
        // Without this, find_first_of below returns 0 and the timestamp is truncated to "".
        while (!end_part.empty() && (end_part.front() == ' ' || end_part.front() == '\t')) {
            end_part.remove_prefix(1);
        }
        // Then strip optional VTT positioning (line:50%, align:start, position:N%)
        // that comes AFTER the second timestamp.
        auto end_space = end_part.find_first_of(" \t");
        if (end_space != std::string_view::npos) end_part = end_part.substr(0, end_space);

        f32 start_s = parse_vtt_timestamp(ts_line.substr(0, arrow));
        f32 end_s = parse_vtt_timestamp(end_part);
        if (!timed_text_adapter_detail::valid_interval(start_s, end_s)) continue;

        // Text lines
        std::string cue_text;
        while (line_idx < lines.size() && !lines[line_idx].empty()) {
            if (!cue_text.empty()) cue_text += '\n';
            cue_text += lines[line_idx];
            ++line_idx;
        }

        if (cue_text.empty()) continue;

        TimedCue cue;
        cue.speaker = extract_vtt_speaker(cue_text);
        cue.text = strip_vtt_tags(cue_text);
        cue.start_s = start_s;
        cue.end_s = end_s;
        cue.source_id = cue_id.empty() ? std::to_string(output.document.cues.size()) : cue_id;

        // Generate word breakdown via the shared uniform-timing core.
        // Byte offsets still reference the TAGGED original line and word
        // tokens exclude inline tags - historical VTT semantics preserved.
        textcore::attach_uniform_words(cue, cue_text,
            /*skip_angle_tags*/true, /*include_cr*/true);

        output.accept(std::move(cue));
    }

    return output.finish();
}

} // namespace chronon3d
