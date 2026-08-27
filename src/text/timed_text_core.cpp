#include "timed_text_core.hpp"
#include <algorithm>
#include <cctype>

namespace chronon3d::textcore {

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto eol = text.find('\n', pos);
        if (eol == std::string_view::npos) { lines.push_back(text.substr(pos)); break; }
        std::size_t len = eol - pos;
        if (len > 0 && text[eol - 1] == '\r') --len;
        lines.push_back(text.substr(pos, len));
        pos = eol + 1;
    }
    return lines;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (prefix.empty() || s.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(s[i])) != std::toupper(static_cast<unsigned char>(prefix[i]))) return false;
    return true;
}

std::vector<WordSpan> split_words(std::string_view text, bool skip_tags, bool include_cr) {
    std::vector<WordSpan> out;
    const auto is_ws = [include_cr](char c) { return c == ' ' || c == '\t' || c == '\n' || (include_cr && c == '\r'); };
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && is_ws(text[i])) ++i;
        if (i >= text.size()) break;
        const std::size_t start = i;
        std::string word;
        bool in_tag = false;
        while (i < text.size() && !is_ws(text[i])) {
            if (skip_tags && text[i] == '<') { in_tag = true; ++i; continue; }
            if (skip_tags && in_tag) { if (text[i] == '>') in_tag = false; ++i; continue; }
            word += text[i++];
        }
        if (!word.empty()) out.push_back({std::move(word), start});
    }
    return out;
}

bool attach_uniform_words(TimedCue& cue, std::string_view buffer, bool skip_tags, bool include_cr) {
    const auto spans = split_words(buffer, skip_tags, include_cr);
    cue.words.clear();
    if (spans.empty()) { cue.word_timing_quality = WordTimingQuality::None; return false; }
    const f32 duration = cue.end_s - cue.start_s;
    const f32 step = duration / static_cast<f32>(spans.size());
    for (std::size_t i = 0; i < spans.size(); ++i) {
        TimedWord word;
        word.text = spans[i].text;
        word.byte_start = spans[i].offset;
        word.byte_end = spans[i].offset + word.text.size();
        word.start_s = cue.start_s + static_cast<f32>(i) * step;
        word.end_s = cue.start_s + static_cast<f32>(i + 1) * step;
        word.semantic_id = cue.source_id + "-word" + std::to_string(i);
        cue.words.push_back(std::move(word));
    }
    cue.word_timing_quality = WordTimingQuality::Estimated;
    return true;
}

WordTimingQuality classify_quality(bool source, std::size_t count) {
    if (source) return WordTimingQuality::Authoritative;
    return count == 0 ? WordTimingQuality::None : WordTimingQuality::Estimated;
}

bool cue_is_valid(const TimedCue& cue) { return cue.start_s >= 0.0f && cue.end_s > cue.start_s && !cue.text.empty(); }

void canonicalize_cue(TimedCue& cue, std::size_t index) {
    if (cue.source_id.empty()) cue.source_id = std::to_string(index + 1);
    if (cue.word_timing_quality == WordTimingQuality::None && !cue.words.empty())
        cue.word_timing_quality = WordTimingQuality::Authoritative;
}

void canonicalize_document(TimedTextDocument& doc) {
    std::vector<TimedCue> valid;
    valid.reserve(doc.cues.size());
    for (auto& cue : doc.cues) {
        canonicalize_cue(cue, valid.size());
        if (cue_is_valid(cue)) valid.push_back(std::move(cue));
    }
    std::stable_sort(valid.begin(), valid.end(), [](const TimedCue& a, const TimedCue& b) {
        if (a.start_s != b.start_s) return a.start_s < b.start_s;
        if (a.end_s != b.end_s) return a.end_s < b.end_s;
        return a.source_id < b.source_id;
    });
    doc.cues = std::move(valid);
}

} // namespace chronon3d::textcore
