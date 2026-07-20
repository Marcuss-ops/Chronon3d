#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// presets/text/subtitle.hpp — Productive subtitle types and helpers
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/math/color.hpp>
#include <chronon3d/text/timed_text_document.hpp>

#include <optional>
#include <string_view>

namespace chronon3d::presets::text {

using SubtitleTrack = TimedTextDocument;
using SubtitleCue = TimedCue;
using TimedWord = ::chronon3d::TimedWord;

struct WordStyleState {
    std::optional<std::size_t> word_index;
    std::string semantic_id;
    float progress{0.0f};
    bool highlighted{false};
    std::optional<Color> color;
    std::optional<float> scale;
    std::optional<Color> background;
    [[nodiscard]] bool operator==(const WordStyleState&) const noexcept = default;
};

inline SubtitleTrack subtitle_from_srt(std::string_view raw) { return timed_text_from_srt(std::string{raw}); }
inline SubtitleTrack subtitle_from_vtt(std::string_view raw) { return timed_text_from_vtt(std::string{raw}); }
inline SubtitleTrack subtitle_from_json(std::string_view raw) { return timed_text_from_json(std::string{raw}); }

inline const SubtitleCue* active_cue_at(const SubtitleTrack& track, float time_s) {
    for (const auto& cue : track.cues) {
        if (time_s >= cue.start_s && time_s < cue.end_s) return &cue;
    }
    return nullptr;
}

inline const TimedWord* active_word_at(const SubtitleCue& cue, float time_s) {
    for (const auto& word : cue.words) {
        if (time_s >= word.start_s && time_s < word.end_s) return &word;
    }
    return nullptr;
}

inline WordStyleState active_word_style_at(
    const SubtitleTrack& track, float time_s,
    std::optional<Color> highlight_color = std::nullopt,
    std::optional<float> highlight_scale = std::nullopt,
    std::optional<Color> highlight_background = std::nullopt)
{
    WordStyleState state;
    const auto* cue = active_cue_at(track, time_s);
    if (!cue || cue->words.empty()) return state;
    const auto* word = active_word_at(*cue, time_s);
    if (!word) return state;
    state.highlighted = true;
    state.semantic_id = word->semantic_id;
    for (std::size_t i = 0; i < cue->words.size(); ++i) {
        if (cue->words[i].semantic_id == word->semantic_id) { state.word_index = i; break; }
    }
    const float duration = word->end_s - word->start_s;
    if (duration > 0.0f) state.progress = (time_s - word->start_s) / duration;
    state.color = highlight_color;
    state.scale = highlight_scale;
    state.background = highlight_background;
    return state;
}

} // namespace chronon3d::presets::text
