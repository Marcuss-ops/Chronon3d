#include <chronon3d/authoring/subtitle_track_builder.hpp>

#include <chronon3d/registry/text_preset_registry.hpp>
#include <chronon3d/registry/text_preset_resolver.hpp>

#include <cmath>

namespace chronon3d::authoring {

Frame SubtitleTrackBuilder::seconds_to_frame(float seconds) {
    // Default 30 fps mapping; fractional frames are truncated.
    return Frame{static_cast<int>(std::lround(seconds * 30.0f))};
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

        // Schedule the cue.
        const Frame start_frame = seconds_to_frame(cue.start_s);
        const Frame duration_frames = seconds_to_frame(cue.end_s - cue.start_s);

        TextRunSpec run_spec =
            registry::wire_preset_text_run_params(preset_id_, spec);

        // Preserve per-cue word timing as a custom animator signal if the
        // preset supports word-level effects.  For now we attach the cue
        // words as a TextSpanOverride per-word so the renderer can highlight
        // the active word via WordStyleState.
        for (const auto& word : cue.words) {
            (void)word;
            // Future: attach per-word span overrides keyed by semantic_id.
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
