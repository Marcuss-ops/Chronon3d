// ─── text_preset_registry.cpp — TextPresetRegistry implementation ───────────
//
// Cluster A DoD #1 (DoD primo-milestone #1 — "20+ preset stabili Reveal /
// Emphasis / Cinematic / Subtitle").  Stage 1 (PR `41cda40c`) shipped
// metadata-only cataloguing of the 5 compositionally-derived entries.
// Stage 2 (PR `ba107a7d`) filled the no-op builders (c3d-001)
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

#include <chronon3d/scene/builders/builder_params.hpp>   // full TextSpec (canonical), TextRunParams, AnimatorResolver types.

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
    // ── Emphasis (4) — FASE 1: delegates to per-category factory ────────
    for (auto& desc : register_helpers_internal::factory_emphasis::create_text_presets()) {
        r.register_preset(std::move(desc));
    }
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
