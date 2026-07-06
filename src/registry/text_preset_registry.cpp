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
#include "text_preset_internal_helpers.hpp"  // M1.5#13 (1/4) — shared factory helpers (NOT installed; lives under src/registry/).

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

// 16. word_pop — FASE 2b — per-word bouncy scale overshoot with Word selector.
// Uses TextSelectorUnit::Word so the pop effect targets each whitespace-delimited
// word independently, matching the preset's semantic intent ("per-word emphasis pop").
[[nodiscard]] inline std::optional<TextAnimatorSpec>
compose_word_pop(const PresetMetadata& /*meta*/) {
    const EasingCurve eo_pop{Easing::OutBack};
    TextAnimatorSpec a = make_presetc_template("word_pop");
    a.properties.push_back(ScaleProperty{Vec3{1.15f, 1.15f, 1.0f}});
    a.properties.push_back(OpacityProperty{1.0f});

    // FASE 2b: Override default global Glyph selector with per-Word selector.
    // Animated `end` sweeps 0→100 over 20f so words pop in sequence.
    a.selectors.clear();
    GlyphSelectorSpec word_sel;
    word_sel.id     = a.id + "_sel_word";
    word_sel.unit   = TextSelectorUnit::Word;
    word_sel.shape  = TextSelectorShape::Square;
    word_sel.start  = AnimatedValue<f32>{0.0f};
    word_sel.end    = AnimatedValue<f32>{
        {{Frame{0},  0.0f,  eo_pop},
         {Frame{20}, 100.0f, eo_pop}}};
    word_sel.amount = AnimatedValue<f32>{100.0f};
    a.selectors.push_back(word_sel);

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


// ── 11. word_pop ──────────────────────────────────────────────────────────
TextPresetDescriptor word_pop_entry() {
    PresetMetadata meta;
    meta.id           = "word_pop";
    meta.display_name = "WordPop";
    meta.category     = TextPresetCategory::Emphasis;
    meta.description  = "Bouncy scale overshoot (1.0 → 1.15) + soft_pop.  "
                         "FASE 2b: now uses a Word selector with animated end sweep "
                         "(0→100 over 20f OutBack) so the pop effect targets each word "
                         "independently in sequence — per-word emphasis pop, "
                         "best in mid-flow body copy.";
    meta.builtin      = true;

    TextPresetDescriptor d;
    d.id              = meta.id;
    d.metadata        = meta;
    d.fixture         = "tests/visual/text/emphasis_word_pop";
    d.builder         = []([[maybe_unused]] SceneBuilderT& sb,
                          LayerBuilderT& lb,
                          const TextSpecT& spec) {
        // FASE 2b: resolver-only — Word selector with animated end sweep
        // drives the per-word pop effect; no layer-level chain methods
        // (consistent with word_cascade/character_cascade pattern).
        (void)wire_through_resolver(lb, "word_pop", spec);
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
namespace register_helpers_internal {

void register_text_preset_cinematic(TextPresetRegistry& r) {
    // ── Cinematic (4) — FASE 1: delegates to per-category factory ───────
    for (auto& desc : register_helpers_internal::factory_cinematic::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
}

void register_text_preset_reveal(TextPresetRegistry& r) {
    // ── Reveal (10) — FASE 1: delegates to per-category factory ─────────
    for (auto& desc : register_helpers_internal::factory_reveal::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
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
    // M1.5#13 (1/4) — Subtitle-category descriptors now come from the basic
    // factory TU (text_preset_factories_basic.cpp).  The factory returns an
    // std::vector<TextPresetDescriptor> in canonical insertion order; the
    // registry bridge here preserves the seed order verbatim.
    for (auto& desc : register_helpers_internal::factory_basic::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
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
    register_helpers_internal::register_builtin_presets(r);
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
        register_helpers_internal::register_builtin_presets(reg);
        reg.freeze();
        return reg;
    }();
    return r;
}

} // namespace chronon3d::registry
