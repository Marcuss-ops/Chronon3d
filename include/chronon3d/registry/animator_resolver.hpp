// ==============================================================================
// include/chronon3d/registry/animator_resolver.hpp (Phase 3.2 — slim)
//
// PUBLIC API — `struct AnimatorResolver`, formerly an anonymous-namespace
// struct in `src/registry/text_preset_registry.cpp`.  Header-lifted per
// TICKET-012 to expose a stable per-id AnimatorSpec-mapping to any TU
// that includes this header (test harness, downstream Cluster B
// authoring facade, canonical resolver-table inspector).
//
// Phase-3.2 mechanical slim: this header is now DECLARATION-ONLY.
// The three static bodies (spec_is_rich + rich_paint_anchor +
// compose_for — the 22-branch resolver table) live in
// `src/registry/animator_resolver.cpp`.  The contract is preserved
// verbatim; behaviour is bit-compatible.
//
// Methods are `static`, so callers don't need to instantiate the struct;
// no shared / mutable state across calls.  Anchor string-ids (e.g.
// "ctc_rich_<preset_id>" and "presetc_<preset_id>") are bound at
// compile time; no runtime registry lookup is performed.
//
// Anti-circular dependency: this header DOES NOT include any
// `content/text/text_*.hpp`.  It pulls the canonical TextSpec +
// TextAnimatorSpec + property types from
// `<chronon3d/scene/builders/builder_params.hpp>`.  External callers
// that instantiate `chronon3d::TextSpec` can pass it to the resolver
// unchanged.
//
// Cross-references:
//   - TICKET-012 — header-lift rationale + acceptance criteria.
//   - docs/ANTI_DUPLICATION_RULES.md — avoid duplicating registries /
//     resolvers / composers / caches; this header-lift consolidates
//     the resolver so the registry TU no longer owns it uniquely.
//   - include/chronon3d/registry/text_preset_resolver.hpp — peer header
//     that exposes the public `wire_preset_text_run_params(...)` free
//     function (which delegates to AnimatorResolver::compose_for).
// ==============================================================================
#pragma once

#include <chronon3d/scene/builders/builder_params.hpp>  // TextSpec, TextAnimatorSpec, Vec3,
                                                          // TextPropertyBlendMode, GlyphSelectorSpec,
                                                          // TextSelectorUnit, TextSelectorShape,
                                                          // OpacityProperty, PositionProperty,
                                                          // ScaleProperty, BlurProperty,
                                                          // TrackingProperty.

#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::registry {

struct AnimatorResolver {
    // ── Phase-3.2 slim: bodies moved to src/registry/animator_resolver.cpp

    /// Stage 4 — fired when the spec carries ANY rich-paint signal
    /// (stroke_enabled / fill_style / stroke_style).  See .cpp for body.
    [[nodiscard]] static bool spec_is_rich(const TextSpec& spec) noexcept;

    /// Stage 4 — `ctc_rich_<preset_id>` anchor TextAnimatorSpec.
    /// See .cpp for body.
    [[nodiscard]] static TextAnimatorSpec
    rich_paint_anchor(std::string_view preset_id);

    /// Stage 5 — Cluster A DoD #2 closure (all 22 presets).  Returns
    /// std::nullopt for `minimal_white` and any unknown id (fail-safe
    /// path that routes to a plain `lb.text(...)`).
    /// See .cpp for the 22-branch table.
    [[nodiscard]] static std::optional<TextAnimatorSpec>
    compose_for(std::string_view preset_id);
};

} // namespace chronon3d::registry
