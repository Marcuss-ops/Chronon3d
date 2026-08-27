#pragma once
#include <chronon3d/text/timed_text_document.hpp>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::textcore {
std::vector<std::string_view> split_lines(std::string_view text);
bool starts_with_ci(std::string_view s, std::string_view prefix);
struct WordSpan { std::string text; std::size_t offset; };
std::vector<WordSpan> split_words(std::string_view text, bool skip_tags, bool include_cr);
bool attach_uniform_words(TimedCue& cue, std::string_view offsets_buffer, bool skip_tags, bool include_cr);
WordTimingQuality classify_quality(bool words_from_source, std::size_t count);
bool cue_is_valid(const TimedCue& cue);

// Canonicalize every adapter output at one boundary: reject invalid timing,
// normalize empty provenance, sort cues deterministically, and normalize IDs.
void canonicalize_document(TimedTextDocument& doc);
void canonicalize_cue(TimedCue& cue, std::size_t index);
} // namespace chronon3d::textcore
