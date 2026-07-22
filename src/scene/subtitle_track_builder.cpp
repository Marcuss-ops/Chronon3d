#include <chronon3d/authoring/subtitle_track_builder.hpp>
#include <chronon3d/core/types/time.hpp>

#include <chronon3d/registry/text_preset_registry.hpp>
#include <chronon3d/registry/text_preset_resolver.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>  // GlyphSelectorSpec, TextSelectorUnit, TextSelectorShape (TICKET-TIMED-WORD-BINDING)

#include <algorithm>
#include <cctype>
#include <cmath>

namespace chronon3d::authoring {

namespace {

// Return the 0-based word index of the whitespace-delimited word that
// starts at `byte_start` in `text`.  This mirrors the word segmentation
// used by the SRT/VTT/JSON adapters, so a TimedWord's byte range can be
// mapped to the TextUnitMap::Word index that the renderer will use.
// Falls back to `fallback` when the exact start offset cannot be found
// (e.g. punctuation-attached words or malformed input).
[[nodiscard]] std::size_t word_index_for_byte_start(
    std::string_view text,
    std::size_t byte_start,
    std::size_t fallback)
{
    std::size_t index = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        // Skip ASCII whitespace (cue text normalises to ASCII separators).
        while (i < text.size() &&
               (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r')) {
            ++i;
        }
        if (i >= text.size()) break;
        if (i == byte_start) return index;
        // Consume non-whitespace word.
        while (i < text.size() &&
               text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
            ++i;
        }
        ++index;
    }
    return fallback;
}

} // namespace

FrameRate SubtitleTrackBuilder::active_frame_rate() const noexcept {
    if (frame_rate_override_.has_value()) {
        return frame_rate_override_.value();
    }
    // SubtitleTrackBuilder is always constructed with a valid LayerBuilder,
    // so the parent composition frame rate is the single source of truth.
    // No fallback default frame rate is provided here.
    return builder_->frame_rate();
}

Frame SubtitleTrackBuilder::seconds_to_frame(float seconds) const {
    return chronon3d::seconds_to_frame(
        static_cast<double>(seconds), active_frame_rate(), FrameRounding::Nearest);
}

std::vector<TimedWordBinding> SubtitleTrackBuilder::build_word_bindings(const TimedCue& cue) {
    std::vector<TimedWordBinding> bindings;
    bindings.reserve(cue.words.size());
    for (std::size_t w = 0; w < cue.words.size(); ++w) {
        const auto& word = cue.words[w];
        const std::size_t resolved_word_index =
            word_index_for_byte_start(cue.text, word.byte_start, w);
        bindings.push_back(TimedWordBinding{
            .semantic_id = word.semantic_id,
            .word_index  = resolved_word_index,
            .total_words = cue.words.size(),
            .byte_start  = word.byte_start,
            .byte_end    = word.byte_end,
            .start_s     = word.start_s,
            .end_s       = word.end_s,
        });
    }
    return bindings;
}

std::vector<GlyphSelectorSpec>
SubtitleTrackBuilder::build_word_selectors(const TimedCue& cue, FrameRate frame_rate, Frame start_frame, std::size_t cue_index) {
    std::vector<GlyphSelectorSpec> selectors;
    if (cue.words.empty()) {
        return selectors;
    }

    const std::size_t word_count = cue.words.size();
    const f32 word_count_f = static_cast<f32>(word_count);

    for (std::size_t w = 0; w < word_count; ++w) {
        const auto& word = cue.words[w];
        const std::size_t word_index =
            word_index_for_byte_start(cue.text, word.byte_start, w);

        const f32 start_pct = (static_cast<f32>(word_index) * 100.0f) / word_count_f;
        const f32 end_pct   = (static_cast<f32>(word_index + 1) * 100.0f) / word_count_f;

        Frame word_start_frame = chronon3d::seconds_to_frame(
            static_cast<double>(word.start_s), frame_rate, FrameRounding::Nearest);
        Frame word_end_frame = chronon3d::seconds_to_frame(
            static_cast<double>(word.end_s), frame_rate, FrameRounding::Nearest);
        if (word_end_frame <= word_start_frame) {
            word_end_frame = word_start_frame + Frame{1};
        }

        GlyphSelectorSpec word_sel;
        word_sel.unit  = TextSelectorUnit::Word;
        word_sel.shape = TextSelectorShape::Square;
        word_sel.order = TextSelectorOrder::Forward;
        word_sel.start = start_pct;
        word_sel.end   = end_pct;
        // The selector id carries the TimedWord semantic_id so the
        // binding from timed-text source to renderer unit is explicit
        // and diagnostics can trace a word back to its source id.
        word_sel.id    = "subtitle_cue_" + std::to_string(cue_index)
                          + "_word_" + word.semantic_id;

        AnimatedValue<f32> amount;
        // Only key "off" before the word if the word does not start
        // exactly at the cue start (avoids two same-frame keys that
        // would hide the 100% value on the active boundary).
        if (start_frame < word_start_frame) {
            amount.add_keyframe(start_frame, 0.0f, EasingCurve{Easing::Hold});
        }
        amount.add_keyframe(word_start_frame, 100.0f, EasingCurve{Easing::Hold});
        amount.add_keyframe(word_end_frame, 0.0f, EasingCurve{Easing::Hold});
        word_sel.amount = std::move(amount);

        selectors.push_back(std::move(word_sel));
    }

    return selectors;
}

void SubtitleTrackBuilder::build() {
    if (!track_ || track_->cues.empty()) {
        return;
    }

    const registry::TextPresetRegistry& preset_registry =
        registry::builtin_text_preset_registry();

    if (!preset_registry.contains(preset_id_)) {
        // Fail-loud: unknown preset id.  The caller should use a registered
        // subtitle preset (minimal_white, yellow_keyword, glow_pulse,
        // caption_box, karaoke_fill, active_word_pop, subtitle_card,
        // lower_third_safe).
        throw std::runtime_error(
            "SubtitleTrackBuilder::build: unknown preset id '" + preset_id_ + "'");
    }

    // Karaoke-style presets rely on per-word timing being trustworthy.
    // By default they require Authoritative per-word timing and reject
    // Estimated/None unless the caller explicitly opts in.
    const bool is_karaoke_preset =
        (preset_id_ == "karaoke_fill" || preset_id_ == "active_word_pop");
    const bool enforce_authoritative =
        (is_karaoke_preset || require_authoritative_) && !allow_estimated_;

    if (enforce_authoritative) {
        for (const auto& cue : track_->cues) {
            if (cue.word_timing_quality != WordTimingQuality::Authoritative) {
                throw std::runtime_error(
                    "SubtitleTrackBuilder: preset '" + preset_id_ +
                    "' requires Authoritative per-word timing. Set "
                    ".allow_estimated_word_timing(true) to opt-in to "
                    "uniform-split word timing.");
            }
        }
    }

    const auto& preset = preset_registry.get(preset_id_);

    for (std::size_t i = 0; i < track_->cues.size(); ++i) {
        const auto& cue = track_->cues[i];
        if (cue.text.empty()) {
            continue;
        }

        TextSpec spec;
        spec.content.value = cue.text;
        spec.font.font_path = font_path_;
        spec.font.font_size = font_size_;
        spec.appearance.color = color_;
        spec.layout.box = box_size_;
        spec.layout.align = align_;

        // Resolve placement through the canonical resolver.
        TextPlacement placement{placement_kind_};
        spec.placement = placement;

        // Schedule the cue using [start_frame, end_frame) semantics.
        const Frame start_frame = seconds_to_frame(cue.start_s);
        const Frame end_frame = seconds_to_frame(cue.end_s);
        const Frame duration_frames = std::max(Frame{1}, end_frame - start_frame);

        TextRunSpec run_spec =
            registry::wire_preset_text_run_params(preset_id_, spec);

        // TICKET-TIMED-WORD-BINDING: emit N per-word selectors and wire
        // them into the preset animator's selector list so the preset's
        // properties (fill / scale / stroke / background) apply ONLY to
        // the active word.
        //
        // Mechanism (Cat-3 minimal-surface, Option c per thinker plan):
        //   * One GlyphSelectorSpec per word with unit=Word; start/end are
        //     percentage ranges (0..100) mapped to word indices by the
        //     TextUnitMap-based selector math.
        //   * Selector math (`evaluate_compiled_selectors`) returns weight=1.0
        //     for the active word and 0.0 for all other words.
        //   * The preset's animator multiplies its properties[] by the
        //     combined selector weight per glyph: the active word gets the
        //     highlight, neighbours stay at base.
        //
        // The selectors live inside the first animator (the preset's own
        // animator returned by wire_preset_text_run_params).  Putting them
        // in TextRunSpec::selectors would orphan them because the
        // materializer evaluates only animator-bound selectors during
        // per-frame evaluation.
        std::vector<GlyphSelectorSpec> word_selectors =
            build_word_selectors(cue, active_frame_rate(), start_frame, i);
        if (!run_spec.animators.empty()) {
            auto& preset_animator = run_spec.animators.front();
            for (auto& word_sel : word_selectors) {
                preset_animator.selectors.push_back(std::move(word_sel));
            }
        }
        // If the preset has no animator (e.g. minimal_white) there is no
        // property to drive per-word highlighting; drop the selectors.

        builder_->animated_text("subtitle_cue_" + std::to_string(i), run_spec)
            .commit()
            .start_at(start_frame)
            .duration(duration_frames);
    }

    // Preset-specific layer-level side effects are applied per-cue above
    // through wire_preset_text_run_params; no extra SceneBuilder call is
    // needed for subtitle presets.
}

} // namespace chronon3d::authoring
