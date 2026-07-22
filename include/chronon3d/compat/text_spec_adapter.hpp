#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_spec_adapter.hpp — Legacy reverse adapters (compat-only)
//
// X2 canonization moved the reverse adapters out of the canonical text module.
// These functions are kept only for backward compatibility in tests and
// external consumers; production code should use PreparedText and the
// canonical compile_text_layout overload.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/text_definition.hpp>
#include <chronon3d/text/text_spec.hpp>
#include <chronon3d/text/text_run_spec.hpp>

namespace chronon3d::compat {

/// Convert the canonical TextDefinition back to a legacy TextSpec.
[[nodiscard]] TextSpec from_text_definition(const TextDefinition& def);

/// Convert the canonical TextDefinition back to a legacy TextRunSpec.
[[nodiscard]] TextRunSpec to_text_run_spec(const TextDefinition& def);

} // namespace chronon3d::compat
