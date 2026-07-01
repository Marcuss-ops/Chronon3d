// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// Cluster A DoD #1 (DoD primo-milestone #1 — "20+ preset stabili Reveal /
// Emphasis / Cinematic / Subtitle").  Stage 1 (PR `41cda40c`) shipped
// metadata-only cataloguing of the 5 compositionally-derived entries.
// Stage 2 (PR `ba107a7d`) filled the no-op builders (TODO(c3d-001))
// with real LayerBuilder API calls.  Stage 3 (PR-c3d-002 copy-modify)
// shipped the 17 addizionali ceiling, reaching 22 entries total
// (≥20+, DoD #1 verde).
//
// ## TEXT-RES-01 refactor (this commit)
// - Struct shape: `TextPreset` → `TextPresetDescriptor` (canonical type,
//   in `text_preset_descriptor.hpp`).  The legacy `TextPreset` typedef
//   in `text_preset_registry.hpp` is preserved as a back-compat shim,
//   so signature churn is kept to the .cpp consumers + 3 test files.
// - The 22 `_entry()` builders now expose TWO membership functions:
//     1. `builder`        — engine-side factory (LayerBuilder wire,
//                           unchanged semantics from Stage 3).
//     2. `animator_factory` — NEW resolver-side factory: pure function
//                           `(const PresetMetadata&) -> std::optional<
//                           TextAnimatorSpec>`.  It absorbs the
//                           hardcoded `else if` branch of the prior
//                           `AnimatorResolver::compose_for` so that
//                           the resolver TU no longer maintains a
//                           per-id table.
// - Each entry also exposes a `fixture` field (golden-frame fixture
//   path) — previously implicit via `// golden-frame-link:` comments.
//
// Anti-circular-dependency: this .cpp DOES NOT include any
// `content/text/text_*.hpp`.  Edge direction canon
// (content → core/registry, mai viceversa) is preserved.
// The header `include/chronon3d/registry/text_preset_registry.hpp` stays
// include-light (forward-decls only); full type definitions of
// SceneBuilder / LayerBuilder / TextSpec are pulled in here, in the .cpp,
// where the builder bodies actually use them.
//
// ── content::text::TextSpec ↔ chronon3d::TextSpec bridge ───────────────
// (TICKET-012) The `content::text::TextSpec` alias lives in
// `include/chronon3d/registry/text_preset_registry.hpp` (single source
// of truth).  No per-TU bridge is needed here.

#include <chronon3d/registry/text_preset_registry.hpp>

#include <chronon3d/registry/text_preset_resolver.hpp>   // Cluster B public API surface
#include <chronon3d/registry/animator_resolver.hpp>      // TICKET-012 — header-lifted public AnimatorResolver.

// TICKET-107 — per-category register helpers exposed to sibling TUs + tests
// (sees the 4 helpers + register_builtin_presets lifted out of the file-local
// anon namespace into `chronon3d::registry::register_helpers_internal`).
#include "text_preset_register_helpers.hpp"

#include <chronon3d/scene/builders/scene_builder.hpp>   // full SceneBuilder
#include <chronon3d/scene/builders/layer_builder.hpp>    // full LayerBuilder
#include <chronon3d/scene/builders/builder_params.hpp>   // full TextSpec (canonical), GlyphSelectorSpec,
                                                          // AnimatedValue<>, EasingCurve, Easing, Frame.

#include <stdexcept>
#include <utility>      // std::move for wire_preset_text_run_params implementation.

namespace chronon3d::registry {

// ── ctor / dtor ────────────────────────────────────────────────────────────
TextPresetRegistry::TextPresetRegistry() = default;

// ── register_preset ─────────────────────────────────────────────────────────
void TextPresetRegistry::register_preset(TextPresetDescriptor preset) {
    if (m_frozen) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: registry is frozen ("
            + (preset.id.empty() ? std::string{"<empty-id>"} : preset.id) + ")");
    }
    if (preset.id.empty()) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: empty id rejected");
    }
    if (m_presets.contains(preset.id)) {
        throw std::runtime_error(
            "TextPresetRegistry::register_preset: duplicate id '"
            + preset.id + "'");
    }
    // Sanity: keep `id` and `metadata.id` in sync (id is duplicated
    // at descriptor top level for O(1) lookahead / inspection).
    if (preset.metadata.id != preset.id) {
        preset.metadata.id = preset.id;
    }
    m_presets.emplace(preset.id, std::move(preset));
}

// ── contains / get ──────────────────────────────────────────────────────────
bool TextPresetRegistry::contains(std::string_view id) const {
    return m_presets.contains(id);
}

const TextPresetDescriptor& TextPresetRegistry::get(std::string_view id) const {
    auto it = m_presets.find(id);
    if (it == m_presets.end()) {
        throw std::runtime_error(
            "TextPresetRegistry::get: unknown preset id '" + std::string{id} + "'");
    }
    return it->second;
}

// ── available / list / by_category ──────────────────────────────────────────
std::vector<std::string> TextPresetRegistry::available() const {
    std::vector<std::string> out;
    out.reserve(m_presets.size());
    for (const auto& [id, _] : m_presets) {
        out.push_back(id);
    }
    return out;  // std::map guarantees sorted-by-key determinism.
}

std::vector<TextPresetDescriptor> TextPresetRegistry::list() const {
    std::vector<TextPresetDescriptor> out;
    out.reserve(m_presets.size());
    for (const auto& [_, preset] : m_presets) {
        out.push_back(preset);
    }
    return out;
}

std::vector<TextPresetDescriptor>
TextPresetRegistry::by_category(TextPresetCategory category) const {
    std::vector<TextPresetDescriptor> out;
    for (const auto& [_, preset] : m_presets) {
        if (preset.metadata.category == category) {
            out.push_back(preset);
        }
    }
    return out;
}

// ── clear / reset ───────────────────────────────────────────────────────────
void TextPresetRegistry::clear() {
    if (m_frozen) return;  // freeze trumps clear.
    m_presets.clear();
}

void TextPresetRegistry::reset() {
    m_presets.clear();
    m_frozen = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// register_builtin_presets — 22 entries (TEXT-RES-01 shape)
// ═══════════════════════════════════════════════════════════════════════════
//
// Each entry exposes 5 fields per the TEXT-RES-01 spec:
//   id, metadata{id,display_name,category,description,builtin},
//   builder, animator_factory, fixture.
//
// `animator_factory` is the resolver-side closure that absorbs the
// hardcoded `else if` branch of the pre-TEXT-RES-01
// `AnimatorResolver::compose_for(preset_id)` table — the resolver
// itself no longer maintains a per-id table; it queries the registry
// and dispatches.
//
// `fixture` is the Visual Regression Harness golden-frame path.
// Convention: fully-qualified path under `tests/visual/` (mirrors
// the pre-refactor `// golden-frame-link:` comment convention).
//
// All 22 entries:
//   ─ REVEAL (10) ─────────────────────────────────────────────────────
//     1.  text_animations            (PR 41cda40c, kept)
//     2.  fade_in                    (Stage 3)
//     3.  blur_in                    (Stage 3)
//     4.  slide_up                   (Stage 3)
//     5.  slide_down                 (Stage 3)
//     6.  scale_in                   (Stage 3)
//     7.  tracking_close             (Stage 3)
//     8.  masked_line_reveal         (Stage 3)
//     9.  word_cascade               (Stage 3)
//     10. character_cascade          (Stage 3)
//
//   ─ EMPHASIS (4) ───────────────────────────────────────────────────
//     11. word_pop                   (Stage 3)
//     12. scale_punch                (Stage 3)
//     13. color_accent               (Stage 3)
//     14. gradient_fill              (Stage 3)
//
//   ─ CINEMATIC (4) ──────────────────────────────────────────────────
//     15. animation_compositions     (PR 41cda40c, kept)
//     16. cinematic_text_camera      (PR 41cda40c, kept)
//     17. cinematic_title_reveal     (PR 41cda40c, kept)
//     18. tilt_sweep_title_v2        (PR 41cda40c, kept)
//
//   ─ SUBTITLE (4) ───────────────────────────────────────────────────
//     19. minimal_white              (Stage 3 — no motion)
//     20. yellow_keyword             (Stage 3)
//     21. glow_pulse                 (Stage 3)
//     22. caption_box                (Stage 3)
namespace {

using LayerBuilderT  = ::chronon3d::LayerBuilder;
using SceneBuilderT  = ::chronon3d::SceneBuilder;
using TextSpecT      = ::chronon3d::TextSpec;

// ── Helper: 22-branch default animator preamble (TEXT-RES-01) ──────────────
//
// Returns the standard `presetc_<id>` TextAnimatorSpec skeleton:
//   - id             = "presetc_<id>"; enabled=true;
//     transform_mode = Add; color_mode = Replace.
//   - selectors      = [GlyphSelectorSpec{id = "<id>_sel_global",
//                                          unit=Glyph, shape=Square,
//                                          start=0, end=100, amount=100}].
//
// Each entry's `animator_factory` calls this at the start and then
// appends the entry-specific properties / selector overrides.  This
// is the FACTORED equivalent of the pre-TEXT-RES-01 `compose_for`
// preamble, kept in ONE place (here) so all 22 entries share the
// canonical scaffold.
TextAnimatorSpec make_presetc_template(std::string_view preset_id) {
    TextAnimatorSpec a;
    a.id             = std::string{"presetc_"} + std::string{preset_id};
    a.enabled        = true;
    a.transform_mode = TextPropertyBlendMode::Add;
    a.color_mode     = TextPropertyBlendMode::Replace;

    GlyphSelectorSpec sel;
    sel.id     = a.id + "_sel_global";
    sel.unit   = TextSelectorUnit::Glyph;
    sel.shape  = TextSelectorShape::Square;
    sel.start  = {0.0f};
    sel.end    = {100.0f};
    sel.amount = {100.0f};
    a.selectors.push_back(sel);

    return a;
}

// ── STAGE 2 helper: minimal_white's no-motion path ──────────────────────────
//
// Compose factory returns std::nullopt — there is no canonical motion
// mapped for `minimal_white`.  The LayerBuilder-side call wires `lb.text_run`
// with `params.animators.empty()` so the TextRunShape driver treats it as
// a static no-op (Sub-case 7-9 invariant).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_minimal_white(const PresetMetadata& /*meta*/) {
    return std::nullopt;
}

// ── STAGE 2 helper: 21 motion-preset compositors (one per non-trivial id) ────
//
// Each helper mirrors the corresponding pre-TEXT-RES-01
// `AnimatorResolver::compose_for(...)` branched body verbatim.  Naming
// pattern: `<id>_animator_compose(meta)` returning
// `std::optional<TextAnimatorSpec>`.  The body uses `make_presetc_template`
// as the preamble then appends entry-specific properties + (when needed)
// selector overrides.

// 1. animation_compositions — depth_reveal(280,45) + soft_pop(30) + float_idle(8,120)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_animation_compositions(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("animation_compositions");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 8.0f, 280.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 2. cinematic_text_camera — depth_reveal(260,50) + scale_drop(0.95,30) + soft_pop(24)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_cinematic_text_camera(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("cinematic_text_camera");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 0.0f, 260.0f}});
    a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 4. cinematic_title_reveal — animated timeline (ease-out cubic, 36f).
// AGENT 2 (TICKET-A2) — replaces prior static ScaleProperty{0.92} + OpacityProperty{1.0f}.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_cinematic_title_reveal(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_cine{Easing::OutCubic};
    TextAnimatorSpec a = make_presetc_template("cinematic_title_reveal");

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.92f, 0.92f, 1.0f}, eo_cine);
    sp.value.add_keyframe(Frame{36}, Vec3{1.0f,  1.0f,  1.0f}, eo_cine);
    a.properties.push_back(sp);

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f, eo_cine);
    op.value.add_keyframe(Frame{36}, 1.0f, eo_cine);
    a.properties.push_back(op);

    BlurProperty bp;
    bp.radius.add_keyframe(Frame{0},  12.0f, eo_cine);
    bp.radius.add_keyframe(Frame{36}, 0.0f,  eo_cine);
    a.properties.push_back(bp);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 40.0f, 0.0f}, eo_cine);
    pp.value.add_keyframe(Frame{36}, Vec3{0.0f, 0.0f,  0.0f}, eo_cine);
    a.properties.push_back(pp);

    return a;
}

// 5. tilt_sweep_title_v2 — scale_drop(1.08,45) + focus_in(2.5,30) + soft_pop(24)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_tilt_sweep_title_v2(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("tilt_sweep_title_v2");
    a.properties.push_back(ScaleProperty{Vec3{1.08f, 1.08f, 1.0f}});
    a.properties.push_back(BlurProperty{2.5f});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 6. text_animations — fade_in(20) + scale_drop(0.95,30)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_text_animations(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("text_animations");
    a.properties.push_back(ScaleProperty{Vec3{0.95f, 0.95f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 7. fade_in — fade_in(15) + soft_pop(10)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_fade_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("fade_in");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 8. blur_in — focus_in(4.0,30) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_blur_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("blur_in");
    a.properties.push_back(BlurProperty{4.0f});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 9. slide_up — fade_shift_vertical({0,200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_up(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("slide_up");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 10. slide_down — fade_shift_vertical({0,-200,0},25) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_slide_down(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("slide_down");
    a.properties.push_back(PositionProperty{Vec3{0.0f, -200.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 11. scale_in — scale_drop(0.85,25) + soft_pop(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_scale_in(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("scale_in");
    a.properties.push_back(ScaleProperty{Vec3{0.85f, 0.85f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 12. tracking_close — AGENT 2 — 35f timeline, tracking 0.18→0 (OutExpo) + opacity 0→1.
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_tracking_close(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("tracking_close");

    TrackingProperty tp;
    tp.pixels.add_keyframe(Frame{0},  0.18f, EasingCurve{Easing::OutExpo});
    tp.pixels.add_keyframe(Frame{35}, 0.0f,  EasingCurve{Easing::Linear});
    a.properties.push_back(tp);

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f, EasingCurve{Easing::Linear});
    op.value.add_keyframe(Frame{35}, 1.0f, EasingCurve{Easing::Linear});
    a.properties.push_back(op);

    return a;
}

// 13. masked_line_reveal — center_split(30) + fade_shift_horizontal({120,0,0},25)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_masked_line_reveal(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("masked_line_reveal");
    a.properties.push_back(PositionProperty{Vec3{120.0f, 0.0f, 0.0f}});
    return a;
}

// 14. word_cascade — AGENT 2 — per-word cascade (Word selector + animated end).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_word_cascade(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_words{Easing::OutCubic};
    TextAnimatorSpec a = make_presetc_template("word_cascade");

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f,  eo_words);
    op.value.add_keyframe(Frame{36}, 1.0f,  eo_words);
    a.properties.push_back(op);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 30.0f, 0.0f}, eo_words);
    pp.value.add_keyframe(Frame{36}, Vec3{0.0f, 0.0f,  0.0f}, eo_words);
    a.properties.push_back(pp);

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.96f, 0.96f, 1.0f}, eo_words);
    sp.value.add_keyframe(Frame{36}, Vec3{1.0f,  1.0f,  1.0f}, eo_words);
    a.properties.push_back(sp);

    // Override default global selector: Word unit + animated `end` (0→100 over 48f).
    a.selectors.clear();
    GlyphSelectorSpec word_sel;
    word_sel.id     = a.id + "_sel_word";
    word_sel.unit   = TextSelectorUnit::Word;
    word_sel.shape  = TextSelectorShape::Square;
    word_sel.start  = AnimatedValue<f32>{0.0f};
    word_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,   eo_words},
         {Frame{48}, 100.0f, eo_words}}};
    word_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(word_sel);

    return a;
}

// 15. character_cascade — AGENT 2 — per-glyph cascade (Glyph selector + animated end).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_character_cascade(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_char{Easing::OutCubic};
    TextAnimatorSpec a = make_presetc_template("character_cascade");

    OpacityProperty op;
    op.value.add_keyframe(Frame{0},  0.0f,  eo_char);
    op.value.add_keyframe(Frame{18}, 1.0f,  eo_char);
    a.properties.push_back(op);

    PositionProperty pp;
    pp.value.add_keyframe(Frame{0},  Vec3{0.0f, 18.0f, 0.0f}, eo_char);
    pp.value.add_keyframe(Frame{18}, Vec3{0.0f, 0.0f,  0.0f}, eo_char);
    a.properties.push_back(pp);

    ScaleProperty sp;
    sp.value.add_keyframe(Frame{0},  Vec3{0.9f, 0.9f, 1.0f}, eo_char);
    sp.value.add_keyframe(Frame{18}, Vec3{1.0f, 1.0f, 1.0f}, eo_char);
    a.properties.push_back(sp);

    // Override default global selector: Glyph unit + animated `end` (0→100 over 24f).
    a.selectors.clear();
    GlyphSelectorSpec glyph_sel;
    glyph_sel.id     = a.id + "_sel_glyph";
    glyph_sel.unit   = TextSelectorUnit::Glyph;
    glyph_sel.shape  = TextSelectorShape::Square;
    glyph_sel.start  = AnimatedValue<f32>{0.0f};
    glyph_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,  eo_char},
         {Frame{24}, 100.0f, eo_char}}};
    glyph_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(glyph_sel);

    return a;
}

// 16. word_pop — scale_drop(1.15,8) + soft_pop(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_word_pop(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("word_pop");
    a.properties.push_back(ScaleProperty{Vec3{1.15f, 1.15f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 17. scale_punch — scale_drop(0.70,12) + soft_pop(20)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_scale_punch(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("scale_punch");
    a.properties.push_back(ScaleProperty{Vec3{0.70f, 0.70f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 18. color_accent — fade_in(12) (colour identity from caller-set spec).
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_color_accent(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("color_accent");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 19. gradient_fill — fade_shift_vertical({0,80,0},20) + fade_in(15)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_gradient_fill(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("gradient_fill");
    a.properties.push_back(PositionProperty{Vec3{0.0f, 80.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 20. yellow_keyword — word_stagger(3,20) + fade_in(12)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_yellow_keyword(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("yellow_keyword");
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// 21. glow_pulse — tracking_breathing(0.08,40)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_glow_pulse(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("glow_pulse");
    a.properties.push_back(TrackingProperty{0.08f});
    return a;
}

// 22. caption_box — fade_shift_vertical({0,-30,0},18) + fade_in(12)
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_caption_box(const PresetMetadata& /*meta*/) {
    TextAnimatorSpec a = make_presetc_template("caption_box");
    a.properties.push_back(PositionProperty{Vec3{0.0f, -30.0f, 0.0f}});
    a.properties.push_back(OpacityProperty{1.0f});
    return a;
}

// ── STAGE 2 helper: shape the 22 entry-builder closures ─────────────────────
// Each closure returns a TextPresetDescriptor populated with: id, metadata,
// builder (engine-side call site), animator_factory (resolver-side closure),
// fixture (golden-frame path).
//
// The `builder` body wires through the helper `wire_through_resolver(lb,
// preset_id, spec).chain_of_motion_calls(...)` for the 17 non-cinematic
// entries — preserved verbatim from PR-c3d-002.  The 4 cinematic entries
// keep their two-stage Stage-4+Stage-5 wiring (rich-paint anchor + canon);
// the 2 "resolver-driven" cinematic entries (`cinematic_title_reveal` +
// `word_cascade` + `character_cascade`) route through resolver only with a
// `(void)wire_through_resolver(...)` cast (matches minimal_white pattern).

// ── Forward declarations (TICKET-012 gcc + PCH workaround) ───────────────────
// Empirically resolves a gcc spurious "'wire_through_resolver' was not
// declared in this scope" diagnostic inside the non-template lambdas when
// `-Winvalid-pch` is active (see TICKET-012 notes).
[[nodiscard]] LayerBuilderT&
wire_through_resolver(LayerBuilderT& lb,
                      std::string_view preset_id,
                      const TextSpecT& spec);

// ── wire_through_resolver — factory-body helper (Cluster A bridge) ─────────
// Thin factory-body helper that delegates to the Cluster B public API
// (`wire_preset_text_run_params`) and then routes the returned
// TextRunParams through `lb.text_run(...).commit()`.  Single canonical
// pipeline: every text preset enters the render graph as a TextRunShape
// (ShapeType::TextRun), regardless of whether the resolver wired a
// TextAnimatorSpec or not.
[[nodiscard]] inline LayerBuilderT&
wire_through_resolver(LayerBuilderT& lb,
                      std::string_view preset_id,
                      const TextSpecT& spec) {
    TextRunParams params =
        ::chronon3d::registry::wire_preset_text_run_params(preset_id, spec);
    const std::string entry_name = std::string{preset_id} + "_text";
    return lb.text_run(entry_name, params).commit();
}


// ── 15. animation_compositions ────────────────────────────────────────────
TextPresetDescriptor animation_compositions_entry() {
    PresetMetadata meta;
    meta.id           = "animation_compositions";
    meta.display_name = "Animation compositions utility suite";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Catalogues helper functions for animation compositions "
                         "(reveal/tilt/word-shimmer factory). Cinematic depth-reveal "
                         "+ soft-pop + float_idle motion preset.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/DeepParallaxCascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "animation_compositions", spec)
          .depth_reveal(280.0f, Frame{45})
          .soft_pop(Frame{30})
          .float_idle(8.0f, Frame{120});
    };
    d.animator_factory = compose_animation_compositions;
    return d;
}

// ── 16. cinematic_text_camera ─────────────────────────────────────────────
TextPresetDescriptor cinematic_text_camera_entry() {
    PresetMetadata meta;
    meta.id           = "cinematic_text_camera";
    meta.display_name = "Cinematic text-camera compositions (5 hero comps)";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "5 hero cinematic compositions (DeepParallaxCascade, "
                         "WhipPanHeroReveal, OrbitHandheldGlow, RackFocusTitleSwap, "
                         "AbyssFreefallStagger). Camera-driven depth reveal. "
                         "Stage 4 (TICKET-A2): resolver-wires a TextAnimatorSpec "
                         "onto the TextRun when the spec carries rich-paint "
                         "signals (stroke_enabled / fill_style / stroke_style).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/camera/camera_visual_tests";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        // Stages 4 + 5 wiring: when spec is rich, push the Stage-4 anchor
        // first (so other content layers compose with `ctc_rich_<id>`), then
        // push the canonical end-state composition.  The descriptor's
        // `animator_factory` now produces the canonical TextAnimatorSpec
        // via the registry-routed
        // `AnimatorResolver::compose_for(<id>)` call below — there is NO
        // per-id branch in the resolver TU.
        TextRunParams params;
        params.text = spec;
        if (::chronon3d::registry::AnimatorResolver::spec_is_rich(spec)) {
            params.animators.push_back(
                ::chronon3d::registry::AnimatorResolver::rich_paint_anchor("cinematic_text_camera"));
        }
        if (auto canonical = ::chronon3d::registry::AnimatorResolver::compose_for("cinematic_text_camera")) {
            params.animators.push_back(*canonical);
        }
        lb.text_run("camera_text", params)
          .commit()
          .depth_reveal(260.0f, Frame{50})
          .scale_drop(0.95f, Frame{30})
          .soft_pop(Frame{24});
    };
    d.animator_factory = compose_cinematic_text_camera;
    return d;
}

// ── 17. cinematic_title_reveal ────────────────────────────────────────────
TextPresetDescriptor cinematic_title_reveal_entry() {
    PresetMetadata meta;
    meta.id           = "cinematic_title_reveal";
    meta.display_name = "Cinematic title reveal (push-in/tilt variants)";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Cinematic title reveal utilities — push-in + tilt "
                         "variants for hero/section titles with subtle drift. "
                         "AGENT 2 (TICKET-A2) — animation is resolver-driven "
                         "(Scale/Opacity/Blur/Position AnimatedValue over 36f "
                         "ease-out cubic); no layer-level chain so the canonical "
                         "resolver path is the single source of keyframes.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/cinematic_title_reveal";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)wire_through_resolver(lb, "cinematic_title_reveal", spec);
    };
    d.animator_factory = compose_cinematic_title_reveal;
    return d;
}

// ── 18. tilt_sweep_title_v2 ───────────────────────────────────────────────
TextPresetDescriptor tilt_sweep_title_v2_entry() {
    PresetMetadata meta;
    meta.id           = "tilt_sweep_title_v2";
    meta.display_name = "Tilt-sweep title v2";
    meta.category     = TextPresetCategory::Cinematic;
    meta.description  = "Tilt-sweep title with cinematic push-in reveal, "
                         "scale animation, and blur ramp — cross-link "
                         "tilt_sweep_title preset family.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/cinematic_motion/tilt_sweep_title_v2";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "tilt_sweep_title_v2", spec)
          .scale_drop(1.08f, Frame{45})
          .focus_in(2.5f, Frame{30})
          .soft_pop(Frame{24});
    };
    d.animator_factory = compose_tilt_sweep_title_v2;
    return d;
}


// ── 1.  text_animations ───────────────────────────────────────────────────
TextPresetDescriptor text_animations_entry() {
    PresetMetadata meta;
    meta.id           = "text_animations";
    meta.display_name = "Text animations utility (typewriter + emphasis)";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Reveal-oriented text animation utilities — typewriter "
                         "+ per-glyph emphasis (word pop, scale punch, gradient fill). "
                         "fade_in + scale_drop entrance.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/PR3/pr3_compositions";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "text_animations", spec)
          .fade_in(Frame{20})
          .scale_drop(0.95f, Frame{30});
    };
    d.animator_factory = compose_text_animations;
    return d;
}

// ── 2.  fade_in ───────────────────────────────────────────────────────────
TextPresetDescriptor fade_in_entry() {
    PresetMetadata meta;
    meta.id           = "fade_in";
    meta.display_name = "FadeIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Pure opacity ramp over Frame{15}, no spatial motion. "
                         "Canonical reveal for body copy / subtitle-level text.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_fade_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "fade_in", spec)
          .fade_in(Frame{15})
          .soft_pop(Frame{10});
    };
    d.animator_factory = compose_fade_in;
    return d;
}

// ── 3.  blur_in ───────────────────────────────────────────────────────────
TextPresetDescriptor blur_in_entry() {
    PresetMetadata meta;
    meta.id           = "blur_in";
    meta.display_name = "BlurIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Focus ramp (4.0 → 0.0 blur) over Frame{30}, paired with "
                         "short fade_in.  Classic motion-graphic reveal pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_blur_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "blur_in", spec)
          .focus_in(4.0f, Frame{30})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_blur_in;
    return d;
}

// ── 4.  slide_up ──────────────────────────────────────────────────────────
TextPresetDescriptor slide_up_entry() {
    PresetMetadata meta;
    meta.id           = "slide_up";
    meta.display_name = "SlideUp";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Vertical slide-up (from below, offset {0,200,0}) + "
                         "fade_in over Frame{25}.  Eyebrow / section-header pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_slide_up";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "slide_up", spec)
          .fade_shift_vertical(Vec3{0.0f, 200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_slide_up;
    return d;
}

// ── 5.  slide_down ────────────────────────────────────────────────────────
TextPresetDescriptor slide_down_entry() {
    PresetMetadata meta;
    meta.id           = "slide_down";
    meta.display_name = "SlideDown";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Vertical slide-down (from above, offset {0,-200,0}) + "
                         "fade_in over Frame{25}.  Bottom-docked subtitle entry.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_slide_down";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "slide_down", spec)
          .fade_shift_vertical(Vec3{0.0f, -200.0f, 0.0f}, Frame{25})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_slide_down;
    return d;
}

// ── 6.  scale_in ──────────────────────────────────────────────────────────
TextPresetDescriptor scale_in_entry() {
    PresetMetadata meta;
    meta.id           = "scale_in";
    meta.display_name = "ScaleIn";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Scale drop (0.85 → 1.0) over Frame{25} + soft_pop "
                         "settle.  Section-header crop pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_scale_in";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "scale_in", spec)
          .scale_drop(0.85f, Frame{25})
          .soft_pop(Frame{15});
    };
    d.animator_factory = compose_scale_in;
    return d;
}

// ── 7.  tracking_close ────────────────────────────────────────────────────
TextPresetDescriptor tracking_close_entry() {
    PresetMetadata meta;
    meta.id           = "tracking_close";
    meta.display_name = "TrackingClose";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Letter-spacing idle pulse (tracking_breathing) over "
                         "Frame{30}.  Use AFTER a parent cinematic preset; "
                         "no entrance motion of its own. AGENT 2 (TICKET-A2) — "
                         "Tracking 0.18→0 over 35f and Opacity 0→1 are "
                         "resolver-driven (AnimatedValue<>); the factory body "
                         "is just the canonical wire-through-resolver path.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_tracking_close";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)wire_through_resolver(lb, "tracking_close", spec);
    };
    d.animator_factory = compose_tracking_close;
    return d;
}

// ── 8.  masked_line_reveal ────────────────────────────────────────────────
TextPresetDescriptor masked_line_reveal_entry() {
    PresetMetadata meta;
    meta.id           = "masked_line_reveal";
    meta.display_name = "MaskedLineReveal";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Center-split mask reveal (lines read out from the "
                         "middle) + horizontal fade_shift.  Mask-and-reveal pattern.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_masked_line";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "masked_line_reveal", spec)
          .center_split(Frame{30})
          .fade_shift_horizontal(Vec3{120.0f, 0.0f, 0.0f}, Frame{25});
    };
    d.animator_factory = compose_masked_line_reveal;
    return d;
}

// ── 9.  word_cascade ──────────────────────────────────────────────────────
TextPresetDescriptor word_cascade_entry() {
    PresetMetadata meta;
    meta.id           = "word_cascade";
    meta.display_name = "WordCascade";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Per-word stagger (Frame{3} delay) + fade_in.  Each "
                         "word enters in succession — the classic kinetic-title "
                         "reveal pattern. AGENT 2 (TICKET-A2) — the Word-unit "
                         "selector carries an AnimatedValue<>`end` that sweeps "
                         "0→100 over 48f, and OpacityProperty/PositionProperty/"
                         "ScaleProperty ramp 0→end_state over 36f ease-out cubic. "
                         "The factory routes through the resolver only — no "
                         "layer-level word_stagger + fade_in chain (single "
                         "canonical path, A2.3).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_word_cascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)wire_through_resolver(lb, "word_cascade", spec);
    };
    d.animator_factory = compose_word_cascade;
    return d;
}

// ── 10. character_cascade ─────────────────────────────────────────────────
TextPresetDescriptor character_cascade_entry() {
    PresetMetadata meta;
    meta.id           = "character_cascade";
    meta.display_name = "CharacterCascade";
    meta.category     = TextPresetCategory::Reveal;
    meta.description  = "Per-glyph stagger (Frame{1} delay) + fade_in.  Tighter "
                         "than WordCascade — best on monospace / kerned display. "
                         "AGENT 2 (TICKET-A2) — Glyph-unit selector with "
                         "AnimatedValue<>`end` (24f) + property ramps over 18f "
                         "ease-out cubic; factory routes through the resolver "
                         "only — no layer-level fade_in + word_stagger chain "
                         "(single canonical path, A2.3).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/reveal_character_cascade";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        (void)wire_through_resolver(lb, "character_cascade", spec);
    };
    d.animator_factory = compose_character_cascade;
    return d;
}


// ── 11. word_pop ──────────────────────────────────────────────────────────
TextPresetDescriptor word_pop_entry() {
    PresetMetadata meta;
    meta.id           = "word_pop";
    meta.display_name = "WordPop";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Bouncy scale overshoot (1.0 → 1.15) + soft_pop.  "
                         "Per-word emphasis pop — best in mid-flow body copy.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_word_pop";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "word_pop", spec)
          .scale_drop(1.15f, Frame{8})
          .soft_pop(Frame{15});
    };
    d.animator_factory = compose_word_pop;
    return d;
}

// ── 12. scale_punch ───────────────────────────────────────────────────────
TextPresetDescriptor scale_punch_entry() {
    PresetMetadata meta;
    meta.id           = "scale_punch";
    meta.display_name = "ScalePunch";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Compression scale_drop (1.0 → 0.7) + soft_pop "
                         "(snap-back).  Impact-punch emphasis.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_scale_punch";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "scale_punch", spec)
          .scale_drop(0.70f, Frame{12})
          .soft_pop(Frame{20});
    };
    d.animator_factory = compose_scale_punch;
    return d;
}

// ── 13. color_accent ──────────────────────────────────────────────────────
TextPresetDescriptor color_accent_entry() {
    PresetMetadata meta;
    meta.id           = "color_accent";
    meta.display_name = "ColorAccent";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Pure fade_in; colour comes from spec.appearance.color "
                         "(set by the caller / pipeline).  Pipe colour-accent "
                         "without touching motion.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_color_accent";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "color_accent", spec)
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_color_accent;
    return d;
}

// ── 14. gradient_fill ─────────────────────────────────────────────────────
TextPresetDescriptor gradient_fill_entry() {
    PresetMetadata meta;
    meta.id           = "gradient_fill";
    meta.display_name = "GradientFill";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Short downward slide + fade_in; gradient paint "
                         "comes from spec.appearance.paint (caller-set).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.fixture         = "tests/visual/text/emphasis_gradient_fill";
    d.metadata        = meta;
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "gradient_fill", spec)
          .fade_shift_vertical(Vec3{0.0f, 80.0f, 0.0f}, Frame{20})
          .fade_in(Frame{15});
    };
    d.animator_factory = compose_gradient_fill;
    return d;
}


// ── 19. minimal_white ─────────────────────────────────────────────────────
TextPresetDescriptor minimal_white_entry() {
    PresetMetadata meta;
    meta.id           = "minimal_white";
    meta.display_name = "MinimalWhite";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "No-motion baseline. Routes through `wire_through_resolver` "
                         "(single canonical path) — empty animators produce a "
                         "static TextRunNode via `materialize_text_run_shape`.  "
                         "Use for static captions or burnt-in subs.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_minimal_white";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        // P1 — minimal_white now goes through the same path as the other
        // 21 built-ins (Sub-cases 7-9 invariants preserved: 1 RenderNode
        // produced when params.animators.empty()).
        (void)wire_through_resolver(lb, "minimal_white", spec);
    };
    d.animator_factory = compose_minimal_white;
    return d;
}

// ── 20. yellow_keyword ────────────────────────────────────────────────────
TextPresetDescriptor yellow_keyword_entry() {
    PresetMetadata meta;
    meta.id           = "yellow_keyword";
    meta.display_name = "YellowKeyword";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Per-word stagger (Frame{3}) + fade_in.  Pair with a "
                         "yellow appearance.color (caller-set) for karaoke-style "
                         "keyword highlighting.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_yellow_keyword";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "yellow_keyword", spec)
          .word_stagger(Frame{3}, Frame{20})
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_yellow_keyword;
    return d;
}

// ── 21. glow_pulse ────────────────────────────────────────────────────────
TextPresetDescriptor glow_pulse_entry() {
    PresetMetadata meta;
    meta.id           = "glow_pulse";
    meta.display_name = "GlowPulse";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Continuous tracking-breathing idle (Frame{40}) — "
                         "subtle on-screen presence.  Pair with a glow "
                         "appearance.material (caller-set).";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_glow_pulse";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "glow_pulse", spec)
          .tracking_breathing(0.08f, Frame{40});
    };
    d.animator_factory = compose_glow_pulse;
    return d;
}

// ── 22. caption_box ───────────────────────────────────────────────────────
TextPresetDescriptor caption_box_entry() {
    PresetMetadata meta;
    meta.id           = "caption_box";
    meta.display_name = "CaptionBox";
    meta.category     = TextPresetCategory::Subtitle;
    meta.description  = "Short downward dock (offset.y=30) + fade_in. "
                         "Pixel-accurate caption-box entry.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/subtitle_caption_box";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        wire_through_resolver(lb, "caption_box", spec)
          .fade_shift_vertical(Vec3{0.0f, -30.0f, 0.0f}, Frame{18})
          .fade_in(Frame{12});
    };
    d.animator_factory = compose_caption_box;
    return d;
}


// FASE 5 (TICKET-107) — register helpers lifted out of this anon namespace into `chronon3d::registry::register_helpers_internal` (see below).  Production callers reach the umbrella via the public `make_default_text_preset_registry()` + `builtin_text_preset_registry()` free functions; tests / sibling internal TUs reach the per-category helpers via `src/registry/text_preset_register_helpers.hpp`.

} // namespace  // FASE 5 (TICKET-107) — anon ends here.

// ── FASE 5 (TICKET-107) — per-category register helpers lifted out of the
// anon namespace into `chronon3d::registry::register_helpers_internal` so
// tests (and any sibling internal TU) can call them in isolation.
namespace chronon3d::registry::register_helpers_internal {

void register_text_preset_cinematic(TextPresetRegistry& r) {
    // ── Cinematic (4) — PR `41cda40c` kept verbatim ──────────────────────
    r.register_preset(animation_compositions_entry());
    r.register_preset(cinematic_text_camera_entry());
    r.register_preset(cinematic_title_reveal_entry());
    r.register_preset(tilt_sweep_title_v2_entry());
}

void register_text_preset_reveal(TextPresetRegistry& r) {
    // ── Reveal (10) — 1 from PR 41cda40c + 9 from Stage 3 ───────────────
    r.register_preset(text_animations_entry());
    r.register_preset(fade_in_entry());
    r.register_preset(blur_in_entry());
    r.register_preset(slide_up_entry());
    r.register_preset(slide_down_entry());
    r.register_preset(scale_in_entry());
    r.register_preset(tracking_close_entry());
    r.register_preset(masked_line_reveal_entry());
    r.register_preset(word_cascade_entry());
    r.register_preset(character_cascade_entry());
}

void register_text_preset_emphasis(TextPresetRegistry& r) {
    // ── Emphasis (4) — Stage 3 ───────────────────────────────────────────
    r.register_preset(word_pop_entry());
    r.register_preset(scale_punch_entry());
    r.register_preset(color_accent_entry());
    r.register_preset(gradient_fill_entry());
}

void register_text_preset_subtitle(TextPresetRegistry& r) {
    // ── Subtitle (4) — Stage 3 ───────────────────────────────────────────
    r.register_preset(minimal_white_entry());
    r.register_preset(yellow_keyword_entry());
    r.register_preset(glow_pulse_entry());
    r.register_preset(caption_box_entry());
}
void register_builtin_presets(TextPresetRegistry& r) {
    // FASE 5 (TICKET-098) — delegate to per-category helpers; order is:
    //   Cinematic (4) → Reveal (10) → Emphasis (4) → Subtitle (4).
    register_text_preset_cinematic(r);
    register_text_preset_reveal(r);
    register_text_preset_emphasis(r);
    register_text_preset_subtitle(r);
}

} // namespace register_helpers_internal

// ── wire_preset_text_run_params — Cluster B public free function ─────────
// Public Cluster B API surface entry point.  Declared in
// include/chronon3d/registry/text_preset_resolver.hpp; see that header
// for full doc + downstream usage patterns.
//
// Implementation note (TEXT-RES-01): the `AnimatorResolver::compose_for`
// call below is now a registry-querying path — there is NO hardcoded
// per-id ELSE-IF branch in the resolver TU.  The resolver queries
// `builtin_text_preset_registry()` (a process-stable accessor), looks
// up the descriptor's `animator_factory` closure, and dispatches it.
// This is the SINGLE source of truth for the per-id mapping — adding
// a 23rd preset is now a single `*_entry()` factory above, NOTHING in
// `src/registry/animator_resolver.cpp` changes.
TextRunParams
wire_preset_text_run_params(std::string_view preset_id,
                            TextSpec spec) noexcept {
    auto composed = ::chronon3d::registry::AnimatorResolver::compose_for(preset_id);
    TextRunParams out;
    out.text = std::move(spec);
    if (composed) {
        out.animators.push_back(std::move(*composed));
    }
    return out;
}

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_builtin_presets(r);
    return r;
}

// ── builtin_text_preset_registry ──────────────────────────────────────────
//
// Process-stable shared instance.  C++11 guarantees thread-safe
// initialization of the magic-static on first call; subsequent calls
// hand back the same `const TextPresetRegistry&` reference.
//
// Frozen contract: post-seed `r.freeze()` ensures any later
// `register_preset` call (e.g. someone monkey-patching from a test
// or a future authoring facade) throws `std::runtime_error`.  Production
// consumers should treat this returned ref as READ-ONLY beyond the
// 22 built-ins.
const TextPresetRegistry&
builtin_text_preset_registry() noexcept {
    static const TextPresetRegistry r = []{
        TextPresetRegistry reg;
        register_builtin_presets(reg);
        reg.freeze();
        return reg;
    }();
    return r;
}

} // namespace chronon3d::registry
