// ─── Chronon3D — TextPresetRegistry public API (Cluster A, DoD #1b) ─────────
//
// Catalyst for DoD primo-milestone #1 ("20+ preset stabili Reveal/Emphasis/
// Cinematic/Subtitle") and unblocked by TICKET-006 (chronon3d_backend_text
// linkage, commit 235c1800).
//
// Mirrors established registry canon in `include/chronon3d/registry/`:
//   - ShapeRegistry:    std::map<std::string, Descriptor, std::less<>> +
//                       register / contains / get / available / list / clear
//   - SamplerRegistry:  same shape with descriptor of function-pointer +
//                       parameter struct
//   - SourceRegistry:   same shape with id-> descriptor
//   - EffectCatalog:    same shape + freeze() (preserved here)
//
// Categorical canon for kinetic-typography presets follows
// `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Fase 10 "Preset library
// produttiva" (Reveal/Emphasis/Cinematic/Subtitle).
//
// ## Tier del canon
// - Stringhe interne: snake_case ASCII, deterministic, no Unicode.
//
// ## Costruttori builder vs builder stampati
// - `TextPresetBuilder` (defined in `text_preset_descriptor.hpp`) =
//   std::function<void(SceneBuilder&, LayerBuilder&, const TextSpec&)>;
//   le firme sono verificate al call site.  I built-in sono real
//   builders seeded via `register_builtin_presets()`.
//
// ## TEXT-RES-01 surface
// - `TextPresenterDescriptor` (canonical type, in
//   `text_preset_descriptor.hpp`) holds:
//     { id, metadata{id,display_name,category,description,builtin},
//       builder, animator_factory, fixture }.
// - `TextPreset` is preserved as a `using` back-compat alias of
//   `TextPresetDescriptor` for downstream consumers / tests that
//   pre-date TEXT-RES-01.  Pre-TEXT-RES-01 field paths `.builtin`,
//   `.description`, `.display_name`, `.category` are now under
//   `.metadata.*` — see "Field-path migration" note below.
//
// ## Field-path migration (TEXT-RES-01 — BREAKING-FOR-TESTS)
//   Pre-refactor:     `preset.builtin`, `preset.description`,
//                      `preset.display_name`, `preset.category`
//   Post-refactor:    `preset.metadata.builtin`,
//                      `preset.metadata.description`,
//                      `preset.metadata.display_name`,
//                      `preset.metadata.category`
//   Top-level preserved:
//                      `preset.id`, `preset.builder`
//                      `preset.animator_factory` (NEW — resolver-side)
//                      `preset.fixture` (NEW — golden-frame path)
//   The legacy `TextPreset::Builder` member-typedef is replaced by
//   the free-standing `TextPresetBuilder` in descriptor.hpp (identical
//   signature).  All builder-lambda call signatures are unchanged.
//
// ## Single-registry anti-duplication-guardrail
//   `AnimatorResolver` queries ONLY `builtin_text_preset_registry()`
//   (declared below).  There is NO per-id table kept in the resolver
//   TU — the resolver's `compose_for(preset_id)` body is a 5-line
//   registry lookup + dispatcher.  See
//   `docs/ANTI_DUPLICATION_RULES.md` §registry/resolver.
//
// Vincoli anti-circular-dependency:
//   Forward-declared in this header: `SceneBuilder`, `LayerBuilder`.
//   Included: `<chronon3d/scene/builders/builder_params.hpp>`
//     (TICKET-012 — supplies the canonical `::chronon3d::TextSpec` that
//      `content::text::TextSpec` aliases in this header).
//   Included: `<chronon3d/registry/text_preset_descriptor.hpp>`
//     (TEXT-RES-01 — supplies canonical `TextPresetDescriptor` +
//      `PresetMetadata` + `TextPresetCategory` + `TextPresetBuilder`
//      + `AnimatorFactory` + `FixtureId`).
//   NOT included here: `scene_builder.hpp`, any `content/text/text_*.hpp`,
//     or any `chronon3d_backends_*` header.  Full SceneBuilder/LayerBuilder
//     definitions are pulled in only inside the .cpp consumer.

#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // canonical ::chronon3d::TextSpec — alias target.
#include <chronon3d/registry/text_preset_descriptor.hpp>  // TEXT-RES-01 — canonical TextPresetDescriptor
                                                          // + TextPresetCategory enum + PresetMetadata
                                                          // + TextPresetBuilder + AnimatorFactory
                                                          // + FixtureId.

#include <functional>  // preserved for std::function signatures reachable via ::chronon3d::TextPresetBuilder.
#include <map>
#include <span>  // M1.5#13 (1/4) — `builtin_text_presets()` span accessor.
#include <string>
#include <string_view>
#include <type_traits>  // explicit — for the static_assert alias-drift guard (TICKET-012).
#include <vector>

namespace chronon3d {

class SceneBuilder;
class LayerBuilder;

namespace content::text {
// TICKET-012 — single source of truth: the public `content::text::TextSpec`
// is a thin alias for the canonical `::chronon3d::TextSpec` from
// builder_params.hpp.  External SDK consumers that instantiate
// `::chronon3d::TextSpec` can pass it directly to any registry Builder.
// The previous forward-decl (`struct TextSpec;`) collided with the
// `using` declaration in the registry TU — the alias must live HERE so
// the std::function signature in this header resolves to the canonical
// type for every consumer without a per-TU bridge.
using TextSpec = ::chronon3d::TextSpec;
// TICKET-012 — alias-drift guard.  If the canonical `::chronon3d::TextSpec`
// is ever renamed or promoted to a different namespace, the std::function
// signature in this header would silently mismatch downstream Builder
// lambdas.  This compile-time assertion catches such drift BEFORE the
// breakage reaches consumers (the canonicalization lives HERE — in this
// public header — so this is the ONLY place an alias-target refactor can
// touch the std::function signature).
static_assert(std::is_same_v<::chronon3d::content::text::TextSpec,
                             ::chronon3d::TextSpec>,
              "content::text::TextSpec alias must resolve to canonical chronon3d::TextSpec");
}

namespace registry {

// ── Back-compat alias (TEXT-RES-01) ─────────────────────────────────────────
//
// `TextPreset` is the legacy name used by the pre-TEXT-RES-01 API; the
// canonical type now lives in `<chronon3d/registry/text_preset_descriptor.hpp>`
// as `TextPresetDescriptor`.  This `using` alias preserves field-path
// stability for downstream consumers (test_text_preset_registry.cpp +
// Cluster B authoring facades + python ctypes render harness) that
// pre-date the TEXT-RES-01 refactor.
//
// Tests that accessed `.category`, `.builtin`, `.display_name`,
// `.description` MUST be updated to `.metadata.X` per the field-path
// migration note at the top of this file.  Tests that accessed
// `.id` and `.builder` continue to compile without edits.
using TextPreset = TextPresetDescriptor;

// ── TextPresetRegistry (singleton-style per shape_registry/SamplerRegistry) ─
//
// SINGLE canonical registry.  AnimatorResolver queries THIS registry
// (via `builtin_text_preset_registry()`) for every per-id lookup;
// there is NO per-id table kept in the resolver TU.  See
// docs/ANTI_DUPLICATION_RULES.md §registry/resolver.
class TextPresetRegistry {
public:
    TextPresetRegistry();

    /// Register a preset. Throws std::runtime_error if the registry is frozen,
    /// if the id is empty, or if the id collides with an existing entry.
    void register_preset(TextPresetDescriptor preset);

    /// Lock the registry — no further `register_preset` calls accepted.
    /// Pattern mirror: EffectCatalog::freeze() (src/effects/effect_catalog.cpp).
    void freeze() noexcept { m_frozen = true; }

    [[nodiscard]] bool is_frozen() const noexcept { return m_frozen; }

    /// O(log n) membership check.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Throws std::runtime_error if id is unknown.
    [[nodiscard]] const TextPresetDescriptor& get(std::string_view id) const;

    /// Returns ids of all registered presets (sorted, deterministic).
    [[nodiscard]] std::vector<std::string> available() const;

    /// Returns all TextPresetDescriptor entries (sorted by id).
    [[nodiscard]] std::vector<TextPresetDescriptor> list() const;

    /// Returns all presets whose `metadata.category == category`.
    [[nodiscard]] std::vector<TextPresetDescriptor>
    by_category(TextPresetCategory category) const;

    /// Erase all non-builtin entries; preserves builtin:false behaviour of
    /// `reset()`. Provided for test isolation.
    void clear();

    /// Erase all entries. Provided for test isolation.
    void reset();

private:
    std::map<std::string, TextPresetDescriptor, std::less<>> m_presets;
    bool m_frozen{false};
};

/// Helper mirroring `make_default_shape_registry()` pattern — populates a
/// FRESH `TextPresetRegistry` with the 22 built-in text presets
/// (Reveal/Emphasis/Subtitle/Cinematic tiers, per
/// `docs/TEXT_AND_KINETIC_TYPOGRAPHY_ROADMAP.md` §Fase 10).  Most
/// production code should use `builtin_text_preset_registry()` (process-
/// stable shared instance) instead; this factory exists for tests that
/// need an isolated, mutable registry.
///
/// Composition flow (the "compute" step the registry delegates to):
///   1. `register_builtin_presets(r)` seeds the `TextPresetDescriptor`
///      table.
///   2. Run-time consumers call `AnimatorResolver::compose_for(preset_id)`
///      (in `include/chronon3d/registry/animator_resolver.hpp`) for the
///      `TextAnimatorSpec` half of the wiring.
///
/// Returns the registry fully populated but UNFROZEN — callers may still
/// register additional presets before `freeze()`.  Production consumers
/// that wish to mutate must call `freeze()` after construction
/// (EffectCatalog parity, enforced at `src/runtime/render_runtime.cpp`
/// for the catalog mirror and at PR-A4's static-singleton fixture in
/// `tests/text/test_text_preset_visual.cpp` for the canonical "default
/// text preset registry" surface today) so `register_preset` throws
/// `std::runtime_error` on any later mutation attempt.
TextPresetRegistry make_default_text_preset_registry();

/// `builtin_text_preset_registry()` returns a process-stable shared
/// instance of the 22 built-in text presets.  This is the SINGLE source
/// of truth that `AnimatorResolver::compose_for(preset_id)` queries — the
/// resolver NEVER constructs or manages its own registry.  Uses a
/// thread-safe magic-static (C++11) to ensure exactly one seed per
/// process regardless of caller concurrency.
///
/// Composition flow:
///   1. First call SEEDS + FREEZES the registry via
///      `register_builtin_presets(r); r.freeze();` (one-shot).
///   2. Subsequent calls return a const reference to the same frozen
///      registry (no allocation, no mutation opportunity).
///
/// Implemented in `src/registry/text_preset_registry.cpp`.
[[nodiscard]] const TextPresetRegistry&
builtin_text_preset_registry() noexcept;

/// `builtin_text_presets()` — span accessor (M1.5#13, 1/4)
///
/// Companion surface to `builtin_text_preset_registry()`: returns a
/// non-owning `std::span<const TextPresetDescriptor>` view of the
/// 22 built-in descriptors (in registry-sorted order).
///
/// Architectural invariants:
///   - Delegated to `builtin_text_preset_registry().list()` so the
///     SINGLE central registry contract (CSR contract from
///     ANTI_DUPLICATION_RULES.md §registry/resolver) remains the
///     canonical source of truth.
///   - The span's lifetime is the process lifetime (the underlying
///     static vector lives forever under the magic-static guarantee).
///   - Companion accessor — does NOT replace
///     `builtin_text_preset_registry()`; responses needing a
///     `const TextPresetRegistry&` (e.g. `by_category`,
///     `contains`) continue to use the registry accessor directly.
///
/// Freeze compliance (AGENTS.md v0.1): zero new registry/preset/
/// sampler; the span is a delegated VIEW onto the existing single
/// registry.  Defined inline here because the implementation is
/// a single Meyers-singleton static + span-return and lives next to
/// the public declaration surface (no .cpp-side definition required).
[[nodiscard]]
inline std::span<const TextPresetDescriptor>
builtin_text_presets() {
    static const std::vector<TextPresetDescriptor> cache =
        builtin_text_preset_registry().list();
    return std::span<const TextPresetDescriptor>(cache);
}

} // namespace chronon3d::registry
} // namespace chronon3d
