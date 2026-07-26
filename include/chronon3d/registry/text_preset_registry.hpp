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
//   std::function<void(SceneBuilder&, LayerBuilder&, const TextDefaults&)>;
//   le firme sono verificate al call site.  I built-in sono real
//   builders seeded via `register_builtin_presets()`.
//
// ## TEXT-RES-01 surface
// - `TextPresenterDescriptor` (canonical type, in
//   `text_preset_descriptor.hpp`) holds:
//     { id, metadata{display_name,category,description,builtin},
//       builder, animator_factory, fixture }.
// - `TextPresetDescriptor` is the only descriptor type. Its metadata fields
//   live under `.metadata.*`; builders use the free-standing
//   `TextPresetBuilder` type from `text_preset_descriptor.hpp`.
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
//   Included: `<chronon3d/registry/text_preset_descriptor.hpp>`
//     (TEXT-RES-01 — supplies canonical `TextPresetDescriptor` +
//      `PresetMetadata` + `TextPresetCategory` + `TextPresetBuilder`
//      + `AnimatorFactory` + `FixtureId`).
//   NOT included here: `scene_builder.hpp`, any `content/text/text_*.hpp`,
//     or any `chronon3d_backends_*` header.  Full SceneBuilder/LayerBuilder
//     definitions are pulled in only inside the .cpp consumer.

#pragma once

#include <chronon3d/registry/text_preset_descriptor.hpp>  // TEXT-RES-01 — canonical TextPresetDescriptor
                                                          // + TextPresetCategory enum + PresetMetadata
                                                          // + TextPresetBuilder + AnimatorFactory
                                                          // + FixtureId.

#include <functional>  // preserved for std::function signatures reachable via ::chronon3d::TextPresetBuilder.
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d {

class SceneBuilder;
class LayerBuilder;

namespace registry {

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
/// FRESH `TextPresetRegistry` with the 28 built-in text presets
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
/// `tests/text/test_text_preset_visual.cpp` for the canonical "default  // drift-allow: stale-ref
/// text preset registry" surface today) so `register_preset` throws
/// `std::runtime_error` on any later mutation attempt.
TextPresetRegistry make_default_text_preset_registry();

/// `builtin_text_preset_registry()` returns a process-stable shared
/// instance of the 28 built-in text presets.  This is the SINGLE source
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

} // namespace chronon3d::registry
} // namespace chronon3d
