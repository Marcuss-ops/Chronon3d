// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// Cluster A DoD #1 (DoD primo-milestone #1 — "20+ preset stabili Reveal /
// Emphasis / Cinematic / Subtitle").  Stage 1 (PR `41cda40c`) shipped
// metadata-only cataloguing of the 5 compositionally-derived entries.
// Stage 2 (PR `ba107a7d`) filled the no-op builders (c3d-001)
// with real LayerBuilder API calls.  Stage 3 (PR-c3d-002 copy-modify)
// shipped the initial addizionali ceiling; the current registry exposes 28 entries total
// (≥20+, DoD #1 verde).
//
// ## TEXT-RES-01 refactor (this commit)
// - `TextPresetDescriptor` is the only descriptor type.
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
// SceneBuilder / LayerBuilder / TextDefaults are pulled in here, in the .cpp,
// where the builder bodies actually use them.
//
#include <chronon3d/registry/text_preset_registry.hpp>

#include <chronon3d/registry/text_preset_resolver.hpp>   // Cluster B public API surface
#include <chronon3d/registry/animator_resolver.hpp>      // TICKET-012 — header-lifted public AnimatorResolver.

// TICKET-107 — per-category register helpers exposed to sibling TUs + tests
// (sees the 4 helpers + register_builtin_presets lifted out of the file-local
// anon namespace into `chronon3d::registry::register_helpers_internal`).
#include "text_preset_register_helpers.hpp"
#include "text_preset_internal_helpers.hpp"  // M1.5#13 (1/4) — shared factory helpers (NOT installed; lives under src/registry/).

#include <chronon3d/scene/builders/builder_params.hpp>   // full TextDefaults (canonical), PreparedText, AnimatorResolver types.

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
// register_builtin_presets — 28 entries (TEXT-RES-01 shape)
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
// All 28 entries:
//   ─ REVEAL (12) ─────────────────────────────────────────────────────
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
//     11. zoom_in                    (CapCut V1 catalog)
//     12. slide_left                 (CapCut V1 catalog)
//
//   ─ EMPHASIS (4) ───────────────────────────────────────────────────
//     13. word_pop                   (Stage 3)
//     14. scale_punch                (Stage 3)
//     15. color_accent               (Stage 3)
//     16. gradient_fill              (Stage 3)
//
//   ─ CINEMATIC (4) ──────────────────────────────────────────────────
//     17. animation_compositions     (PR 41cda40c, kept)
//     18. cinematic_text_camera      (PR 41cda40c, kept)
//     19. cinematic_title_reveal     (PR 41cda40c, kept)
//     20. tilt_sweep_title_v2        (PR 41cda40c, kept)
//
//   ─ SUBTITLE (4) ───────────────────────────────────────────────────
//     21. minimal_white              (Stage 3 — no motion)
//     22. yellow_keyword             (Stage 3)
//     23. glow_pulse                 (Stage 3)
//     24. caption_box                (Stage 3)

namespace register_helpers_internal {

void register_text_preset_cinematic(TextPresetRegistry& r) {
    // ── Cinematic (4) — FASE 1: delegates to per-category factory ───────
    for (auto& desc : register_helpers_internal::factory_cinematic::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
}

void register_text_preset_reveal(TextPresetRegistry& r) {
    // ── Reveal (12) — FASE 1: delegates to per-category factory ─────────
    for (auto& desc : register_helpers_internal::factory_reveal::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
}

void register_text_preset_emphasis(TextPresetRegistry& r) {
    // ── Emphasis (4) — FASE 1: delegates to per-category factory ────────
    for (auto& desc : register_helpers_internal::factory_emphasis::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
}

void register_text_preset_subtitle(TextPresetRegistry& r) {
    // ── Subtitle (8) — productive subtitle catalog ─────────────────────
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
    //   Cinematic (4) → Reveal (12) → Emphasis (4) → Subtitle (8).
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
PreparedText
wire_preset_text_run_params(std::string_view preset_id,
                            TextDefaults spec) noexcept {
    PreparedText out;
    out.document.utf8 = spec.content.value;
    out.document.defaults = std::move(spec);
    out.document.spans.clear();
    out.document.split_paragraphs(out.document.defaults.layout.paragraph);
    out.style.font = out.document.defaults.font;
    out.style.color = out.document.defaults.appearance.color;
    out.style.paint = out.document.defaults.appearance.paint;
    out.style.shadows = out.document.defaults.appearance.shadows;
    out.style.material = out.document.defaults.appearance.material;
    out.style.box_style = out.document.defaults.appearance.box_style;
    out.frame.size = out.document.defaults.layout.box;
    out.frame.placement = out.document.defaults.placement;
    out.frame.anchor = out.document.defaults.layout.anchor;
    out.frame.align = out.document.defaults.layout.align;
    out.frame.vertical_align = out.document.defaults.layout.vertical_align;
    out.frame.wrap = out.document.defaults.layout.wrap;
    out.frame.overflow = out.document.defaults.layout.overflow;
    out.frame.centering_mode = out.document.defaults.layout.centering_mode;
    out.frame.line_height = out.document.defaults.layout.line_height;
    out.frame.tracking = out.document.defaults.layout.tracking;
    out.frame.auto_fit = out.document.defaults.layout.auto_fit;
    out.frame.min_font_size = out.document.defaults.layout.min_font_size;
    out.frame.max_font_size = out.document.defaults.layout.max_font_size;
    out.frame.max_lines = out.document.defaults.layout.max_lines;
    out.frame.ellipsis = out.document.defaults.layout.ellipsis;
    if (auto composed = ::chronon3d::registry::AnimatorResolver::compose_for(preset_id)) {
        out.animation.animators.push_back(std::move(*composed));
    }
    return out;
}

PreparedText
wire_preset_text_run_params(std::string_view preset_id,
                            const TextDefinition& definition) noexcept {
    PreparedText out = prepare_text(definition);
    if (auto composed = ::chronon3d::registry::AnimatorResolver::compose_for(preset_id)) {
        out.animation.animators.push_back(std::move(*composed));
    }
    return out;
}

// ── make_default_text_preset_registry ──────────────────────────────────────
TextPresetRegistry make_default_text_preset_registry() {
    TextPresetRegistry r;
    register_helpers_internal::register_builtin_presets(r);
    r.freeze();
    return r;
}

// ── builtin_text_preset_registry ──────────────────────────────────────────
//
// Built-in catalog storage is a single immutable namespace-scope object.
// The accessor is intentionally only a view; production consumers cannot
// mutate the frozen catalog through this API.
const TextPresetRegistry kBuiltinTextPresetRegistry =
    make_default_text_preset_registry();

const TextPresetRegistry&
builtin_text_preset_registry() noexcept {
    return kBuiltinTextPresetRegistry;
}

} // namespace chronon3d::registry
