// ═══════════════════════════════════════════════════════════════════════════
// StyleRegistry — string-keyed factory for `TextStyle` presets.
//
// Public subclass of `detail::BasicRegistry<TextStyle>`. Adds the
// domain-specific `register_style(id, value)` alias.  All lookup /
// lifecycle / threading semantics live in `BasicRegistry` — read its
// header for the lifecycle contract (factory re-evaluation, no
// re-entrancy, silent override, no eviction).
//
// Maps string ids (e.g. `"youtube.hero.premium"`, `"news.lower-third"`)
// to a registered style. Resolved values are returned as
// `chronon3d::TextStyle` — the same struct used by the existing
// brace-init layer API.
//
// ── Anti-duplication ────────────────────────────────────────────────────
//   • Returns the existing `chronon3d::TextStyle` (no parallel struct).
//   • Implementation sharing via `detail::BasicRegistry<Value>` avoids
//     the ~95% code duplication seen in early PR 5 drafts and reduces
//     the audit surface for lifecycle / threading bugs.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/authoring/detail/basic_registry.hpp>
#include <chronon3d/scene/model/shape/shape.hpp>   // chronon3d::TextStyle

#include <string>

namespace chronon3d::authoring {

class StyleRegistry : public detail::BasicRegistry<chronon3d::TextStyle> {
public:
    using detail::BasicRegistry<chronon3d::TextStyle>::BasicRegistry;

    // Domain-specific alias: register_style(id, value).
    // Forwards to the generic `register_value` of BasicRegistry and
    // returns *this so chaining returns the derived ref. We can't
    // `return register_value(...)` directly: BasicRegistry::register_value
    // returns a base ref (BasicRegistry<TextStyle>&) and `register_value`
    // is NOT virtual, so covariant downcast from base&→derived& is not
    // allowed. Forwarding via side-effect + `return *this` is the
    // canonical workaround.
    StyleRegistry& register_style(std::string id, chronon3d::TextStyle value) & {
        this->register_value(std::move(id), std::move(value));
        return *this;
    }

    // `register_factory`, `resolve`, `has`, `unregister`, `clear`,
    // `count`, `ids` are inherited verbatim from `BasicRegistry`.
};

} // namespace chronon3d::authoring
