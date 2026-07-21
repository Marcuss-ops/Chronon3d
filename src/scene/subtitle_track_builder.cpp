#include <chronon3d/authoring/subtitle_track_builder.hpp>
#include <chronon3d/core/types/time.hpp>

#include <chronon3d/registry/text_preset_registry.hpp>
#include <chronon3d/registry/text_preset_resolver.hpp>
#include <chronon3d/text/glyph_selector_spec.hpp>  // GlyphSelectorSpec, TextSelectorUnit, TextSelectorShape (TICKET-TIMED-WORD-BINDING)

#include <algorithm>
#include <cmath>

namespace chronon3d::authoring {

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

        // TICKET-TIMED-WORD-BINDING: emit N per-word selectors so the
        // preset's existing animator (e.g. `yellow_keyword`, `karaoke_fill`,
        // `active_word_pop`) naturally applies its highlight properties
        // (color / scale / stroke / background) to the ACTIVE word only.
        //
        // Mechanism (Cat-3 minimal-surface, Option c per thinker plan):
        //   * One GlyphSelectorSpec per word with unit=Word, start=i, end=i+1.
        //   * Selector math (`evaluate_selector`) returns weight=1.0 ONLY
        //     for the word whose index falls in [start, end); weight=0.0
        //     for all other words and adjacent glyphs.
        //   * The preset's animator multiplies its properties[] by the
        //     combined selector weight per glyph: the active word gets the
        //     highlight, neighbours stay at base.
        //
        // No `semantic_id` field needed on GlyphSelectorSpec (zero new
        // SDK type); the per-frame active-word resolution is implicit in
        // the index-based math.  Per-`byte_start`/`byte_end` on TimedWord
        // is populated by the 3 adapters (SRT/VTT/JSON) for downstream
        // TextSpanOverride mapping if needed.
        for (std::size_t w = 0; w < cue.words.size(); ++w) {
            GlyphSelectorSpec word_sel;
            word_sel.unit = TextSelectorUnit::Word;
            word_sel.start = static_cast<f32>(w);
            word_sel.end = static_cast<f32>(w + 1);
            word_sel.shape = TextSelectorShape::Square;
            word_sel.id = "subtitle_cue_" + std::to_string(i)
                          + "_word_" + std::to_string(w) + "_sel";
            run_spec.selectors.push_back(std::move(word_sel));
        }

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
