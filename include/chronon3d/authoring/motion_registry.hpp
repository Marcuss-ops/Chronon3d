// ═══════════════════════════════════════════════════════════════════════════
// MotionRegistry — string-keyed factory for `TextAnimatorSpec` presets.
//
// Public subclass of `detail::BasicRegistry<TextAnimatorSpec>`. Adds the
// domain-specific `register_motion(id, value)` alias.  All lookup /
// lifecycle / threading semantics live in `BasicRegistry` — read its
// header for the lifecycle contract.
//
// Maps string ids (e.g. `"text.reveal.soft"`, `"text.tracking.tighten"`)
// to a registered motion. Resolved values are returned as
// `chronon3d::TextAnimatorSpec` — the same struct produced by `Animator`
// (PR 1) and consumed by the existing per-glyph animation pipeline.
//
// ── Domain separation vs `chronon3d::presets::motion::MotionPresetRegistry` ───
//
//   `presets::motion::MotionPresetRegistry` is keyed by enum and returns
//   `MotionPresetDescriptor` (camera / generic-property motion).
//   `authoring::MotionRegistry` is keyed by string and returns
//   `TextAnimatorSpec` (per-glyph text animation presets).
//   Different domains → both coexist per `ANTI_DUPLICATION_RULES.md` §2.
//
// ── Anti-duplication ────────────────────────────────────────────────────
//   • Returns the existing `chronon3d::TextAnimatorSpec` (no parallel
//     spec type).
//   • Implementation sharing via `detail::BasicRegistry<Value>` mirrors
//     the `StyleRegistry` arrangement and centralises lifecycle /
//     threading audit in one place.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/authoring/detail/basic_registry.hpp>
#include <chronon3d/text/text_animator_property.hpp>  // chronon3d::TextAnimatorSpec

#include <string>

namespace chronon3d::authoring {

class MotionRegistry : public detail::BasicRegistry<chronon3d::TextAnimatorSpec> {
public:
    using detail::BasicRegistry<chronon3d::TextAnimatorSpec>::BasicRegistry;

    // Domain-specific alias: register_motion(id, value).
    // Forwards to the generic `register_value` of BasicRegistry and
    // returns *this so chaining returns the derived ref. We can't
    // `return register_value(...)` directly: BasicRegistry::register_value
    // returns a base ref (BasicRegistry<TextAnimatorSpec>&) and
    // `register_value` is NOT virtual, so covariant downcast from
    // base&→derived& is not allowed. Forwarding via side-effect +
    // `return *this` is the canonical workaround.
    MotionRegistry& register_motion(std::string id, chronon3d::TextAnimatorSpec value) & {
        this->register_value(std::move(id), std::move(value));
        return *this;
    }

    // `register_factory`, `resolve`, `has`, `unregister`, `clear`,
    // `count`, `ids` are inherited verbatim from `BasicRegistry`.
};

} // namespace chronon3d::authoring
