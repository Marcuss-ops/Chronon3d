// ==============================================================================
// include/chronon3d/render_plan/visual_preset_materializer.hpp
//
// VISUAL-SSOT-02 — the SINGLE visual preset materializer.
//
// Consumes the existing `VisualPresetRegistry` (there is no second registry)
// and lowers one `LayerPlan` onto a fully-resolved overlay primitive:
//
//   LayerPlan
//       ↓ lookup descriptor (profile-resolved)
//   base_preset (canonical text materializer)
//       ↓
//   preset defaults → style overrides → font asset
//       ↓
//   animation intent (registry defaults + plan overrides)
//       ↓
//   layout INTENT (anchor + fallback + content bounds)
//       ↓
//   ResolvedVisualLayer { text, animation, layout, preset_id }
//
// The scene builder / renderer consumes ONLY the resolved primitive — it
// never sees the editorial profile (discovery/young/crime), the semantic
// role (PERSON/ORG/IMPORTANT_WORD), or the preset→materializer mapping: those
// live in the registry, not in the consumer.
//
// Final placement is NOT done here: the layout resolver maps the resolved
// intent + bounds to concrete x/y as a separate scene-wide phase (see
// render_plan_compiler.cpp `resolve_visual_layout` → OverlayLayoutResolver).
// ==============================================================================

#pragma once

#include <chronon3d/render_plan/animation_intent.hpp>  // ResolvedAnimation
#include <chronon3d/text/resolve_text_placement.hpp>   // CanvasInfo
#include <chronon3d/text/text_definition.hpp>          // TextDefinition

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::render_plan {
struct LayerPlan;  // fwd — render_plan.hpp.
}

namespace chronon3d::registry {
class VisualPresetRegistry;
struct AnimationSpec;
}

namespace chronon3d::render_plan {

/// Layout INTENT resolved by the materializer — NOT final coordinates. The
/// `OverlayLayoutResolver` maps this intent + the content bounds to concrete
/// x/y in a separate scene phase (collision avoidance + fallback there).
struct ResolvedLayoutIntent {
    std::string intent;                  // preferred anchor type
    std::vector<std::string> fallback_intents;  // preferred → fallback order
    float safe_margin{0.06f};            // canvas fraction reserved per side
    float width{0.0f};                   // content bounds (canvas pixels)
    float height{0.0f};
};

/// Fully-resolved overlay primitive.  Every field is concrete: this is what
/// a worker actually renders, free of editorial/profile knowledge.
struct ResolvedVisualLayer {
    std::string preset_id;
    chronon3d::TextDefinition text;
    ResolvedAnimation animation;
    ResolvedLayoutIntent layout;
};

/// Fully-resolved IMAGE overlay (the ImageVisualMaterializer output).  Image
/// presets carry no text materializer; they resolve only their layout intent
/// (anchor + fallback + content bounds) and animation intent.  The image
/// bytes/size/fit stay on the LayerPlan (they are asset routing, not preset
/// knowledge).
struct ResolvedImageLayer {
    std::string preset_id;
    ResolvedLayoutIntent layout;
    ResolvedAnimation animation;
};

/// Real content bounds in canvas pixels, measured BEFORE layout resolution.
/// The `OverlayLayoutResolver` places overlays by their actual footprint —
/// shaped text width + font metrics + card padding + stroke/shadow — not by
/// a canvas-fraction layout box (a lower-third "Tim Cook" is ~410×92, not
/// the full 1640×100 caption box).
struct VisualBounds {
    float width{0.0f};
    float height{0.0f};
};

/// Measure the real content bounds of a resolved TEXT overlay: per-line
/// HarfBuzz shaping (width) + font vertical metrics (height, with the
/// preset's line-height multiplier) + card padding + centered stroke +
/// shadow blur/offset extent.  Falls back to the preset's authored layout
/// box when the font cannot be loaded or the text is empty, so the resolver
/// always receives a non-zero footprint.
[[nodiscard]] VisualBounds measure_visual_bounds(
    const ResolvedVisualLayer& layer,
    chronon3d::FontEngine& engine);

/// SINGLE canonical visual preset materializer.
///
/// `style_profile` selects the registry profile (discovery/young/crime);
/// `registry` is the one `VisualPresetRegistry` (never a second table);
/// `composition_frames` is the fallback layer window length.
///
/// Throws std::runtime_error when the preset is not a text-layer preset or
/// carries no text materialization (fail-loud, no silent generic-text
/// fallback for a known preset id).
class VisualPresetMaterializer {
public:
    [[nodiscard]] ResolvedVisualLayer materialize(
        const LayerPlan& layer,
        const chronon3d::CanvasInfo& canvas,
        std::string_view style_profile,
        const registry::VisualPresetRegistry& registry,
        Frame composition_frames) const;

    /// Materialize an IMAGE preset (e.g. image_focus_in) from the same
    /// registry: resolves the anchor intent + animation intent so image
    /// presets are executed, not just described.  Throws std::runtime_error
    /// when the preset is not an image-layer preset.
    [[nodiscard]] ResolvedImageLayer materialize_image(
        const LayerPlan& layer,
        const chronon3d::CanvasInfo& canvas,
        std::string_view style_profile,
        const registry::VisualPresetRegistry& registry,
        Frame composition_frames) const;
};

/// Lower a resolved animation intent onto a TextDefinition's per-unit animator
/// stack.  `preset_animation` is the registry descriptor's AnimationSpec
/// (nullopt for legacy plan layers without a visual preset).  This is shared
/// by the materializer (registry path) and the render-plan compiler (legacy
/// non-preset path) so both emit the identical per-unit reveal.
void apply_text_animation_intent(
    chronon3d::TextDefinition& definition,
    const LayerPlan& layer,
    const std::optional<registry::AnimationSpec>& preset_animation,
    std::int64_t composition_frames);

}  // namespace chronon3d::render_plan
