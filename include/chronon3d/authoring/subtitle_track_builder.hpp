#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// authoring/subtitle_track_builder.hpp — Fluent subtitle track authoring
//
// Consumes the canonical SubtitleTrack (TimedTextDocument) and produces
// one scheduled text-run per cue on the parent LayerBuilder.  Keeps
// TimedTextDocument the single source of truth for timing and adapters.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/presets/text/subtitle.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/text/resolve_text_placement.hpp>

#include <string>
#include <string_view>

namespace chronon3d::authoring {

class Layer;

/// Builds scheduled text-runs from a SubtitleTrack.
///
/// Usage:
///   layer.subtitles(track)
///        .preset("karaoke_fill")
///        .font("fonts/Poppins-Bold.ttf", 48.0f)
///        .color(Color::White)
///        .box({1400.0f, 200.0f})
///        .align(TextAlign::Center)
///        .place(TextPlacementKind::SafeAreaBottom);
class SubtitleTrackBuilder {
public:
    SubtitleTrackBuilder(LayerBuilder& builder,
                          const CanvasInfo& canvas,
                          const presets::text::SubtitleTrack& track)
        : builder_(&builder), canvas_(&canvas), track_(&track) {}

    /// Choose a text preset from the registry (default: minimal_white).
    SubtitleTrackBuilder& preset(std::string_view preset_id) {
        preset_id_ = std::string{preset_id};
        return *this;
    }

    /// Set the font path and size for every cue.
    SubtitleTrackBuilder& font(std::string_view path, float size) {
        font_path_ = std::string{path};
        font_size_ = size;
        return *this;
    }

    /// Set the fill colour for every cue.
    SubtitleTrackBuilder& color(const Color& c) {
        color_ = c;
        return *this;
    }

    /// Set the layout box size for every cue.
    SubtitleTrackBuilder& box(const Vec2& size) {
        box_size_ = size;
        return *this;
    }

    /// Set horizontal alignment for every cue.
    SubtitleTrackBuilder& align(TextAlign a) {
        align_ = a;
        return *this;
    }

    /// Set placement kind for every cue.
    SubtitleTrackBuilder& place(TextPlacementKind kind) {
        placement_kind_ = kind;
        return *this;
    }

    /// Commit the track: create one text-run per cue with timing.
    /// Cues are translated from seconds to frames using the default frame
    /// rate (30 fps) unless the caller has set a frame rate on the builder.
    void build();

private:
    LayerBuilder* builder_{nullptr};
    const CanvasInfo* canvas_{nullptr};
    const presets::text::SubtitleTrack* track_{nullptr};

    std::string preset_id_{"minimal_white"};
    std::string font_path_{"fonts/Poppins-Bold.ttf"};
    float font_size_{48.0f};
    Color color_{1.0f, 1.0f, 1.0f, 1.0f};
    Vec2 box_size_{1400.0f, 200.0f};
    TextAlign align_{TextAlign::Center};
    TextPlacementKind placement_kind_{TextPlacementKind::SafeAreaBottom};

    /// Convert seconds to Frame using a default 30 fps mapping.
    [[nodiscard]] static Frame seconds_to_frame(float seconds);

    friend class Layer;
};

} // namespace chronon3d::authoring
