// ═══════════════════════════════════════════════════════════════════════════
// src/text/timed_text_parser.cpp — TXT-11 Timed Text Parser implementations
//
// Implements SRT, WebVTT, and JSON word-timing parsers producing the
// canonical SubtitleTrack / SubtitleCue / WordTiming model.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/timed_text_parser.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::text {

using presets::text::SubtitleTrack;
using presets::text::SubtitleCue;
using presets::text::WordTiming;

namespace {

// ── Time helpers ───────────────────────────────────────────────────────────

/// Parse HH:MM:SS,mmm (SRT) or HH:MM:SS.mmm (VTT) into total seconds.
/// Returns negative on parse failure.
inline double parse_timecode_to_seconds(std::string_view tc) {
    // Expected: HH:MM:SS[,.]mmm  or  MM:SS[,.]mmm
    int h = 0, m = 0, s = 0, ms = 0;
    // Try SRT separator (comma) first, then VTT (dot)
    auto comma_pos = tc.rfind(',');
    auto dot_pos   = tc.rfind('.');
    std::size_t sep_pos = std::string_view::npos;
    if (comma_pos != std::string_view::npos) sep_pos = comma_pos;
    else if (dot_pos != std::string_view::npos) sep_pos = dot_pos;
    else return -1.0;

    std::string_view time_part = tc.substr(0, sep_pos);
    std::string_view ms_part   = tc.substr(sep_pos + 1);

    // Count colons to determine format
    int colons = 0;
    for (char c : time_part) if (c == ':') ++colons;

    try {
        if (colons == 2) {
            // HH:MM:SS
            auto c1 = time_part.find(':');
            auto c2 = time_part.find(':', c1 + 1);
            h = std::stoi(std::string{time_part.substr(0, c1)});
            m = std::stoi(std::string{time_part.substr(c1 + 1, c2 - c1 - 1)});
            s = std::stoi(std::string{time_part.substr(c2 + 1)});
        } else if (colons == 1) {
            // MM:SS
            auto c1 = time_part.find(':');
            m = std::stoi(std::string{time_part.substr(0, c1)});
            s = std::stoi(std::string{time_part.substr(c1 + 1)});
        } else {
            return -1.0;
        }
        ms = std::stoi(std::string{ms_part});
    } catch (...) {
        return -1.0;
    }

    return static_cast<double>(h) * 3600.0
         + static_cast<double>(m) * 60.0
         + static_cast<double>(s)
         + static_cast<double>(ms) / 1000.0;
}

/// Convert seconds to Frame at the given fps.
inline Frame seconds_to_frame(double sec, int fps) {
    return Frame{static_cast<int>(std::round(sec * static_cast<double>(fps)))};
}

// ── String helpers ─────────────────────────────────────────────────────────

/// Trim leading and trailing whitespace.
inline std::string_view trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
    return sv;
}

/// Split a string_view by newlines into lines.
inline std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

/// Strip VTT inline tags like <c.class>, <b>, <i>, <u>, <v.name>.
inline std::string strip_vtt_tags(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool in_tag = false;
    for (char c : text) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) out.push_back(c);
    }
    return out;
}

/// Parse inline SRT word timing: <t=start_ms>word<t=end_ms>
/// Example: <t=0>Hello<t=1500> <t=1500>world<t=4000>
/// @param cue_end_sec  The cue's end time in seconds, used for the last word's end.
inline std::vector<WordTiming> parse_srt_inline_words(
    std::string_view text, double cue_start_sec, double cue_end_sec, int fps) {
    std::vector<WordTiming> words;
    // Simple state-machine parser
    std::size_t pos = 0;
    while (pos < text.size()) {
        // Look for <t=
        auto tag_start = text.find("<t=", pos);
        if (tag_start == std::string_view::npos) break;
        auto tag_end = text.find('>', tag_start);
        if (tag_end == std::string_view::npos) break;

        // Parse time value
        std::string_view time_str = text.substr(tag_start + 3, tag_end - tag_start - 3);
        int ms = 0;
        try { ms = std::stoi(std::string{time_str}); } catch (...) { break; }

        // Word text is between this > and next < (or end)
        auto word_start = tag_end + 1;
        auto next_tag = text.find('<', word_start);
        std::string_view word_text;
        if (next_tag == std::string_view::npos) {
            word_text = trim(text.substr(word_start));
            pos = text.size();
        } else {
            word_text = trim(text.substr(word_start, next_tag - word_start));
            pos = next_tag;
        }

        if (!word_text.empty()) {
            double word_start_sec = static_cast<double>(ms) / 1000.0;
            WordTiming wt;
            wt.word  = std::string{word_text};
            wt.start = seconds_to_frame(cue_start_sec + word_start_sec, fps);
            // End time filled below from next word's start or cue end
            wt.end   = wt.start + Frame{1};
            words.push_back(std::move(wt));
        } else {
            pos = next_tag;
        }
    }

    // Fill in end times from next word's start, with last word using cue end
    for (std::size_t i = 0; i + 1 < words.size(); ++i) {
        words[i].end = words[i + 1].start;
    }
    if (!words.empty()) {
        words.back().end = seconds_to_frame(cue_end_sec, fps);
    }
    return words;
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// SRT Parser
// ═══════════════════════════════════════════════════════════════════════════

ParseResult parse_srt(std::string_view content, int fps) {
    ParseResult result;
    auto lines = split_lines(content);

    enum class State { ExpectIndex, ExpectTimestamp, ExpectText };
    State state = State::ExpectIndex;
    SubtitleCue current_cue;
    std::string cue_text;
    int expected_index = 1;

    for (const auto& raw_line : lines) {
        auto line = trim(raw_line);

        // Skip BOM
        if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) continue;

        switch (state) {
        case State::ExpectIndex: {
            if (line.empty()) continue; // skip blank lines before index
            // Try to parse as integer index
            int idx = 0;
            try { idx = std::stoi(std::string{line}); } catch (...) {
                result.error = "SRT: expected cue index at line containing: " + std::string{line};
                return result;
            }
            if (idx != expected_index) {
                // Non-fatal: accept any index
            }
            expected_index = idx + 1;
            state = State::ExpectTimestamp;
            break;
        }
        case State::ExpectTimestamp: {
            // Expected: "HH:MM:SS,mmm --> HH:MM:SS,mmm"
            auto arrow = line.find("-->");
            if (arrow == std::string_view::npos) {
                result.error = "SRT: expected timestamp with '-->' at: " + std::string{line};
                return result;
            }
            auto start_tc = trim(line.substr(0, arrow));
            auto end_tc   = trim(line.substr(arrow + 3));

            double start_sec = parse_timecode_to_seconds(start_tc);
            double end_sec   = parse_timecode_to_seconds(end_tc);
            if (start_sec < 0 || end_sec < 0) {
                result.error = "SRT: invalid timestamp: " + std::string{line};
                return result;
            }

            current_cue = SubtitleCue{};
            current_cue.start = seconds_to_frame(start_sec, fps);
            current_cue.end   = seconds_to_frame(end_sec, fps);
            cue_text.clear();
            state = State::ExpectText;
            break;
        }
        case State::ExpectText: {
            if (line.empty()) {
                // End of cue
                current_cue.text = cue_text;
                // Parse inline word timing if present
                double start_sec = static_cast<double>(current_cue.start.value)
                                 / static_cast<double>(fps);
                double end_sec   = static_cast<double>(current_cue.end.value)
                                 / static_cast<double>(fps);
                current_cue.words = parse_srt_inline_words(cue_text, start_sec, end_sec, fps);
                if (!current_cue.text.empty() || !current_cue.words.empty()) {
                    result.track.cues.push_back(std::move(current_cue));
                }
                current_cue = SubtitleCue{};
                cue_text.clear();
                state = State::ExpectIndex;
            } else {
                if (!cue_text.empty()) cue_text += ' ';
                cue_text += strip_vtt_tags(line);
            }
            break;
        }
        }
    }

    // Flush last cue if still in text state
    if (state == State::ExpectText && (!cue_text.empty() || !current_cue.words.empty())) {
        current_cue.text = cue_text;
        double start_sec = static_cast<double>(current_cue.start.value)
                         / static_cast<double>(fps);
        double end_sec   = static_cast<double>(current_cue.end.value)
                         / static_cast<double>(fps);
        current_cue.words = parse_srt_inline_words(cue_text, start_sec, end_sec, fps);
        if (!current_cue.text.empty() || !current_cue.words.empty()) {
            result.track.cues.push_back(std::move(current_cue));
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// WebVTT Parser
// ═══════════════════════════════════════════════════════════════════════════

ParseResult parse_vtt(std::string_view content, int fps) {
    ParseResult result;
    auto lines = split_lines(content);

    enum class State { ExpectHeader, ExpectCueOrNote, ExpectTimestamp, ExpectText };
    State state = State::ExpectHeader;
    SubtitleCue current_cue;
    std::string cue_text;
    bool saw_webvtt = false;

    for (const auto& raw_line : lines) {
        auto line = trim(raw_line);

        // Skip BOM
        if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF) continue;

        switch (state) {
        case State::ExpectHeader: {
            if (line.empty()) continue;
            // Check for "WEBVTT" header (case-insensitive prefix)
            std::string lower;
            lower.reserve(line.size());
            for (char c : line) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower.starts_with("webvtt")) {
                saw_webvtt = true;
                state = State::ExpectCueOrNote;
            } else if (!saw_webvtt) {
                // No WEBVTT header — treat as malformed but continue
                state = State::ExpectCueOrNote;
                // Re-process this line
                goto expect_cue_line;
            }
            break;
        }
        case State::ExpectCueOrNote: {
expect_cue_line:
            if (line.empty()) continue;
            // Skip NOTE blocks
            std::string lower;
            lower.reserve(line.size());
            for (char c : line) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower.starts_with("note")) continue;
            if (lower.starts_with("style")) continue;
            // If line contains timestamp arrow, parse it immediately
            // (avoids the TXT-11 VTT bug: timestamp was consumed but never parsed)
            auto arrow = line.find("-->");
            if (arrow != std::string_view::npos) {
                auto start_tc = trim(line.substr(0, arrow));
                auto end_tc   = trim(line.substr(arrow + 3));
                auto space_after = end_tc.find(' ');
                if (space_after != std::string_view::npos)
                    end_tc = end_tc.substr(0, space_after);
                double start_sec = parse_timecode_to_seconds(start_tc);
                double end_sec   = parse_timecode_to_seconds(end_tc);
                if (start_sec < 0 || end_sec < 0) {
                    result.error = "VTT: invalid timestamp: " + std::string{line};
                    return result;
                }
                current_cue = SubtitleCue{};
                current_cue.start = seconds_to_frame(start_sec, fps);
                current_cue.end   = seconds_to_frame(end_sec, fps);
                cue_text.clear();
                state = State::ExpectText;
            }
            // else: it's a cue identifier line — skip and wait for timestamp
            break;
        }
        case State::ExpectTimestamp: {
            if (line.empty()) { state = State::ExpectCueOrNote; continue; }
            auto arrow = line.find("-->");
            if (arrow == std::string_view::npos) {
                // Might be a cue ID followed by timestamp — skip
                continue;
            }
            auto start_tc = trim(line.substr(0, arrow));
            auto end_tc   = trim(line.substr(arrow + 3));

            // VTT may have extra settings after end timestamp (position, align, etc.)
            auto space_after = end_tc.find(' ');
            if (space_after != std::string_view::npos) {
                end_tc = end_tc.substr(0, space_after);
            }

            double start_sec = parse_timecode_to_seconds(start_tc);
            double end_sec   = parse_timecode_to_seconds(end_tc);
            if (start_sec < 0 || end_sec < 0) {
                result.error = "VTT: invalid timestamp: " + std::string{line};
                return result;
            }

            current_cue = SubtitleCue{};
            current_cue.start = seconds_to_frame(start_sec, fps);
            current_cue.end   = seconds_to_frame(end_sec, fps);
            cue_text.clear();
            state = State::ExpectText;
            break;
        }
        case State::ExpectText: {
            if (line.empty()) {
                // End of cue
                if (!cue_text.empty()) {
                    current_cue.text = strip_vtt_tags(cue_text);
                    // Distribute words: split text into words with evenly-spaced timing
                    std::string stripped = strip_vtt_tags(cue_text);
                    std::istringstream iss(stripped);
                    std::vector<std::string> word_list;
                    std::string w;
                    while (iss >> w) word_list.push_back(w);
                    if (!word_list.empty()) {
                        double start_sec = static_cast<double>(current_cue.start.value)
                                         / static_cast<double>(fps);
                        double end_sec   = static_cast<double>(current_cue.end.value)
                                         / static_cast<double>(fps);
                        double duration = end_sec - start_sec;
                        double per_word = duration / static_cast<double>(word_list.size());
                        for (std::size_t i = 0; i < word_list.size(); ++i) {
                            WordTiming wt;
                            wt.word  = word_list[i];
                            wt.start = seconds_to_frame(start_sec + per_word * static_cast<double>(i), fps);
                            wt.end   = seconds_to_frame(start_sec + per_word * static_cast<double>(i + 1), fps);
                            current_cue.words.push_back(std::move(wt));
                        }
                    }
                }
                if (!current_cue.text.empty()) {
                    result.track.cues.push_back(std::move(current_cue));
                }
                current_cue = SubtitleCue{};
                cue_text.clear();
                state = State::ExpectCueOrNote;
            } else {
                if (!cue_text.empty()) cue_text += '\n';
                cue_text += std::string{line};
            }
            break;
        }
        }
    }

    // Flush last cue
    if (state == State::ExpectText && !cue_text.empty()) {
        current_cue.text = strip_vtt_tags(cue_text);
        if (!current_cue.text.empty()) {
            result.track.cues.push_back(std::move(current_cue));
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// JSON Word-Timing Parser
// ═══════════════════════════════════════════════════════════════════════════

ParseResult parse_json_word_timing(std::string_view content, int fps) {
    ParseResult result;
    // Minimal JSON parser — handles the well-defined word-timing schema without
    // a full JSON library dependency.  Parses only the expected structure.

    // Find "cues" array
    auto cues_pos = content.find("\"cues\"");
    if (cues_pos == std::string_view::npos) {
        result.error = "JSON: missing \"cues\" key";
        return result;
    }

    // Find opening bracket of array
    auto array_start = content.find('[', cues_pos);
    if (array_start == std::string_view::npos) {
        result.error = "JSON: expected '[' after \"cues\"";
        return result;
    }

    // Simple state-machine JSON parser for the word-timing schema
    std::size_t pos = array_start + 1;
    int depth = 1; // We're inside the cues array

    while (pos < content.size() && depth > 0) {
        // Skip whitespace
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\n'
               || content[pos] == '\r' || content[pos] == '\t')) ++pos;
        if (pos >= content.size()) break;

        if (content[pos] == ']') { --depth; ++pos; continue; }
        if (content[pos] == ',') { ++pos; continue; }

        if (content[pos] == '{') {
            // Parse a cue object
            auto obj_end = content.find('}', pos);
            if (obj_end == std::string_view::npos) {
                result.error = "JSON: unclosed object";
                return result;
            }
            std::string_view obj = content.substr(pos, obj_end - pos + 1);
            pos = obj_end + 1;

            SubtitleCue cue;

            // Extract fields: start, end, text, words
            auto extract_number = [&](std::string_view key) -> double {
                auto k = obj.find(key);
                if (k == std::string_view::npos) return -1.0;
                auto colon = obj.find(':', k);
                if (colon == std::string_view::npos) return -1.0;
                auto val_start = colon + 1;
                while (val_start < obj.size() && (obj[val_start] == ' ')) ++val_start;
                auto val_end = val_start;
                while (val_end < obj.size() && (std::isdigit(static_cast<unsigned char>(obj[val_end]))
                       || obj[val_end] == '.' || obj[val_end] == '-' || obj[val_end] == 'e'
                       || obj[val_end] == 'E')) ++val_end;
                try {
                    return std::stod(std::string{obj.substr(val_start, val_end - val_start)});
                } catch (...) {
                    return -1.0;
                }
            };

            auto extract_string = [&](std::string_view key) -> std::string {
                auto k = obj.find(key);
                if (k == std::string_view::npos) return {};
                auto quote1 = obj.find('"', k + key.size());
                if (quote1 == std::string_view::npos) return {};
                auto quote2 = obj.find('"', quote1 + 1);
                if (quote2 == std::string_view::npos) return {};
                return std::string{obj.substr(quote1 + 1, quote2 - quote1 - 1)};
            };

            double start_sec = extract_number("\"start\"");
            double end_sec   = extract_number("\"end\"");
            if (start_sec < 0 || end_sec < 0) {
                result.error = "JSON: cue missing start/end";
                return result;
            }

            cue.start = seconds_to_frame(start_sec, fps);
            cue.end   = seconds_to_frame(end_sec, fps);
            cue.text  = extract_string("\"text\"");

            // Parse "words" array
            auto words_key = obj.find("\"words\"");
            if (words_key != std::string_view::npos) {
                auto words_array = obj.find('[', words_key);
                if (words_array != std::string_view::npos) {
                    std::size_t wp = words_array + 1;
                    int wdepth = 1;
                    while (wp < obj.size() && wdepth > 0) {
                        while (wp < obj.size() && (obj[wp] == ' ' || obj[wp] == '\n'
                               || obj[wp] == '\r' || obj[wp] == '\t')) ++wp;
                        if (wp >= obj.size()) break;
                        if (obj[wp] == ']') { --wdepth; ++wp; continue; }
                        if (obj[wp] == ',') { ++wp; continue; }
                        if (obj[wp] == '{') {
                            auto wend = obj.find('}', wp);
                            if (wend == std::string_view::npos) break;
                            std::string_view wobj = obj.substr(wp, wend - wp + 1);
                            wp = wend + 1;

                            // Extract word fields from this sub-object
                            auto wextract_string = [&](std::string_view key) -> std::string {
                                auto k = wobj.find(key);
                                if (k == std::string_view::npos) return {};
                                auto q1 = wobj.find('"', k + key.size());
                                if (q1 == std::string_view::npos) return {};
                                auto q2 = wobj.find('"', q1 + 1);
                                if (q2 == std::string_view::npos) return {};
                                return std::string{wobj.substr(q1 + 1, q2 - q1 - 1)};
                            };
                            auto wextract_number = [&](std::string_view key) -> double {
                                auto k = wobj.find(key);
                                if (k == std::string_view::npos) return -1.0;
                                auto colon = wobj.find(':', k);
                                if (colon == std::string_view::npos) return -1.0;
                                auto vs = colon + 1;
                                while (vs < wobj.size() && wobj[vs] == ' ') ++vs;
                                auto ve = vs;
                                while (ve < wobj.size() && (std::isdigit(static_cast<unsigned char>(wobj[ve]))
                                       || wobj[ve] == '.' || wobj[ve] == '-')) ++ve;
                                try {
                                    return std::stod(std::string{wobj.substr(vs, ve - vs)});
                                } catch (...) { return -1.0; }
                            };

                            WordTiming wt;
                            wt.word  = wextract_string("\"word\"");
                            double ws = wextract_number("\"start\"");
                            double we = wextract_number("\"end\"");
                            if (!wt.word.empty() && ws >= 0 && we >= 0) {
                                wt.start = seconds_to_frame(ws, fps);
                                wt.end   = seconds_to_frame(we, fps);
                                cue.words.push_back(std::move(wt));
                            }
                        } else {
                            ++wp; // skip unknown character
                        }
                    }
                }
            }

            if (!cue.text.empty() || !cue.words.empty()) {
                result.track.cues.push_back(std::move(cue));
            }
        } else {
            ++pos; // skip unknown
        }
    }

    return result;
}

} // namespace chronon3d::text
