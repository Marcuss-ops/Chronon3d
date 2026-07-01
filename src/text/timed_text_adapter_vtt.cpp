#include <chronon3d/text/timed_text_document.hpp>

#include <algorithm>
#include <cctype>
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
    TimedTextDocument doc;
    doc.source_format = "vtt";

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
        size_t len = eol - pos;
        if (len > 0 && content[eol - 1] == '\r') --len;
        lines.push_back(content.substr(pos, len));
        pos = eol + 1;
    }

    size_t line_idx = 0;

    // Skip WEBVTT header
    if (line_idx < lines.size()) {
        std::string_view hdr(lines[line_idx]);
        // Case-insensitive check for "WEBVTT"
        if (hdr.size() >= 6) {
            bool is_webvtt = true;
            for (int i = 0; i < 6; ++i) {
                if (std::toupper(static_cast<unsigned char>(hdr[i])) != "WEBVTT"[i]) {
                    is_webvtt = false;
                    break;
                }
            }
            if (is_webvtt) {
                ++line_idx;

                // Optional metadata after WEBVTT header until blank line or cue start
                while (line_idx < lines.size() && !lines[line_idx].empty()) {
                    auto& meta = lines[line_idx];
                    if (meta.find("Language:") == 0 || meta.find("language:") == 0) {
                        auto colon = meta.find(':');
                        if (colon != std::string_view::npos) {
                            doc.language = std::string(meta.substr(colon + 1));
                            // Trim
                            auto& lang = doc.language;
                            while (!lang.empty() && (lang.front() == ' ' || lang.front() == '\t')) lang.erase(0, 1);
                            while (!lang.empty() && (lang.back() == ' ' || lang.back() == '\t')) lang.pop_back();
                        }
                    } else if (meta.find("Title:") == 0 || meta.find("title:") == 0) {
                        auto colon = meta.find(':');
                        if (colon != std::string_view::npos) {
                            doc.title = std::string(meta.substr(colon + 1));
                            auto& t = doc.title;
                            while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
                            while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
                        }
                    }
                    ++line_idx;
                }
            }
        }
    }

    // Skip blank lines and style blocks after header
    while (line_idx < lines.size()) {
        while (line_idx < lines.size() && lines[line_idx].empty()) ++line_idx;
        if (line_idx >= lines.size()) break;

        // Skip STYLE blocks
        std::string_view ll(lines[line_idx]);
        if (ll.size() > 6) {
            bool is_style = true;
            for (int i = 0; i < 5; ++i) {
                if (std::toupper(static_cast<unsigned char>(ll[i])) != "STYLE"[i]) {
                    is_style = false;
                    break;
                }
            }
            if (is_style && (ll[5] == ':' || ll[5] == ' ')) {
                // Skip until blank line
                while (line_idx < lines.size() && !lines[line_idx].empty()) ++line_idx;
                continue;
            }
        }

        // Optional cue id (non-empty, non-timestamp line before timestamp)
        std::string cue_id;
        auto& maybe_id = lines[line_idx];
        if (!maybe_id.empty() && maybe_id.find("-->") == std::string_view::npos) {
            cue_id = std::string(maybe_id);
            ++line_idx;

            // Skip NOTE comments
            if (cue_id.size() >= 4) {
                bool is_note = true;
                for (int i = 0; i < 4; ++i) {
                    if (std::toupper(static_cast<unsigned char>(cue_id[i])) != "NOTE"[i]) {
                        is_note = false;
                        break;
                    }
                }
                if (is_note) {
                    while (line_idx < lines.size() && !lines[line_idx].empty()) ++line_idx;
                    continue;
                }
            }
        }

        if (line_idx >= lines.size()) break;

        // Timestamp line
        std::string_view ts_line = lines[line_idx++];
        auto arrow = ts_line.find("-->");
        if (arrow == std::string_view::npos) continue;

        // VTT supports optional positioning after the arrow — trim it
        std::string_view end_part = ts_line.substr(arrow + 3);
        auto end_space = end_part.find_first_of(" \t");
        if (end_space != std::string_view::npos) end_part = end_part.substr(0, end_space);

        f32 start_s = parse_vtt_timestamp(ts_line.substr(0, arrow));
        f32 end_s = parse_vtt_timestamp(end_part);
        if (start_s < 0.0f || end_s < 0.0f || end_s <= start_s) continue;

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
        cue.source_id = cue_id.empty() ? std::to_string(doc.cues.size()) : cue_id;

        // Generate word breakdown
        auto wranges = [&cue_text]() {
            std::vector<std::pair<std::string, size_t>> wr;
            size_t i = 0;
            while (i < cue_text.size()) {
                while (i < cue_text.size() && (cue_text[i] == ' ' || cue_text[i] == '\t' || cue_text[i] == '\n' || cue_text[i] == '\r')) ++i;
                if (i >= cue_text.size()) break;
                size_t start = i;
                // Skip VTT tags first
                std::string raw;
                bool in_tag = false;
                while (i < cue_text.size()) {
                    if (cue_text[i] == '<') { in_tag = true; ++i; continue; }
                    if (cue_text[i] == '>' && in_tag) { in_tag = false; ++i; continue; }
                    if (!in_tag && (cue_text[i] == ' ' || cue_text[i] == '\t' || cue_text[i] == '\n' || cue_text[i] == '\r')) break;
                    if (!in_tag) raw += cue_text[i];
                    ++i;
                }
                if (!raw.empty()) wr.push_back({raw, start});
            }
            return wr;
        }();

        int word_idx = 0;
        for (const auto& [word_text, offset] : wranges) {
            (void) offset;
            TimedWord tw;
            tw.text = word_text;
            f32 cue_dur = end_s - start_s;
            f32 word_dur = cue_dur / static_cast<f32>(wranges.size());
            tw.start_s = start_s + static_cast<f32>(word_idx) * word_dur;
            tw.end_s = start_s + static_cast<f32>(word_idx + 1) * word_dur;
            tw.semantic_id = cue.source_id + "-word" + std::to_string(word_idx);
            cue.words.push_back(std::move(tw));
            ++word_idx;
        }

        doc.cues.push_back(std::move(cue));
    }

    return doc;
}

} // namespace chronon3d
