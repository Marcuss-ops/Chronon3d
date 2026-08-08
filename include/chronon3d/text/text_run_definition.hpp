#pragma once

// ═══════════════════════════════════════════════════════════════════════════
// text_run_definition.hpp — Runtime text-run definition.
//
// This payload combines a TextDefaults document substrate with the
// per-glyph animation stack used by the text compiler.
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/text/prepared_text.hpp>

namespace chronon3d {

// Compatibility name only.  PreparedText is the sole text-run transport;
// legacy callers keep compiling while all production materialization routes
// through materialize_prepared_text().
using TextRunDefinition = PreparedText;

} // namespace chronon3d
