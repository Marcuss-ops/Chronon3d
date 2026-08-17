// ─── Chronon3D — VisualPresetRegistry public API (VISUAL-SSOT-01) ──────────
//
// SINGLE canonical registry for overlay-level visual presets.  This is the
// one place that knows how each preset is painted, anchored and animated:
//
//   caption_card / active_word_pop / subtitle_card / lower_third_safe /
//   organization_card / location_card / image_focus_in plus the 2D showcase
//   families image_* / name_* / phrase_* (five choices per family).
//
// PipelineGen only resolves semantic_role → preset_id (editorial decision,
// kept in its SemanticOverlayResolver); RenderingGen transports and
// executes; Chronon resolves the preset_id to its full default style via
// THIS registry (see ADR-029).
//
// Mirrors established registry canon in `include/chronon3d/registry/`:
//   - ShapeRegistry / TextPresetRegistry: std::map<std::string,
//     Descriptor, std::less<>> + register / contains / get / available /
//     list / clear / reset, plus freeze() (TextPresetRegistry parity).
//
// ## Single-registry anti-duplication-guardrail
//   There is exactly ONE VisualPresetRegistry.  There is NO second visual
//   registry in RenderingGen, and no per-id table kept anywhere else — a
//   resolver queries `builtin_visual_preset_registry()` and dispatches.

#pragma once

#include <chronon3d/registry/visual_preset_descriptor.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::registry {

// ── VisualPresetRegistry (singleton-style, TextPresetRegistry parity) ──────
class VisualPresetRegistry {
public:
    VisualPresetRegistry();

    /// Register a preset. Throws std::runtime_error if the registry is frozen,
    /// if the id is empty, or if the id collides with an existing entry.
    void register_preset(VisualPresetDescriptor preset);

    /// Lock the registry — no further `register_preset` calls accepted.
    /// Pattern mirror: TextPresetRegistry::freeze().
    void freeze() noexcept { m_frozen = true; }

    [[nodiscard]] bool is_frozen() const noexcept { return m_frozen; }

    /// O(log n) membership check.
    [[nodiscard]] bool contains(std::string_view id) const;

    /// Throws std::runtime_error if id is unknown.
    [[nodiscard]] const VisualPresetDescriptor& get(std::string_view id) const;

    /// Resolve a visual preset with one of the canonical editorial profiles.
    /// The returned descriptor is a value copy because profile resolution may
    /// replace its style and animation while preserving the base preset id.
    [[nodiscard]] VisualPresetDescriptor get_for_profile(
        std::string_view id, std::string_view profile) const;

    /// Returns ids of all registered presets (sorted, deterministic).
    [[nodiscard]] std::vector<std::string> available() const;

    /// Returns all VisualPresetDescriptor entries (sorted by id).
    [[nodiscard]] std::vector<VisualPresetDescriptor> list() const;

    /// Erase all entries unless frozen. Provided for test isolation.
    void clear();

    /// Erase all entries and unfreeze. Provided for test isolation.
    void reset();

private:
    std::map<std::string, VisualPresetDescriptor, std::less<>> m_presets;
    bool m_frozen{false};
};

/// Populate a FRESH `VisualPresetRegistry` with the built-in visual presets
/// and freeze it.  Most production code should use
/// `builtin_visual_preset_registry()` (process-stable shared instance);
/// this factory exists for tests that need an isolated registry.
VisualPresetRegistry make_default_visual_preset_registry();

/// `builtin_visual_preset_registry()` returns a process-stable shared
/// instance of the built-in visual presets.  This is the SINGLE source of
/// truth for "how a preset is rendered" — resolvers query it, they never
/// construct or manage their own visual-preset table.  Implemented in
/// `src/registry/visual_preset_registry.cpp` as a namespace-scope const
/// object (constant-initialized, thread-safe).
[[nodiscard]] const VisualPresetRegistry&
builtin_visual_preset_registry() noexcept;

} // namespace chronon3d::registry
