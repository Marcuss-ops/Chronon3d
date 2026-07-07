#pragma once

// ── Text Placement Test Suite — Content Compositions ───────────────────────
//
// Canonical test compositions that exercise the TextRun placement pipeline:
// pin_to(Anchor::Center) + centered_text(...) with NO .pos workaround.
//
// These are registerable via CompositionRegistry so they can be rendered
// from the CLI (`chronon3d_cli render TextPlaceStaticCenter`) AND used
// by the golden test harness (tests/text_golden/text_placement/).
//
// Design rule (enforced by the golden tests):
//   - NO composition uses .pos = {960,540,0} as a centering workaround
//   - ALL composition use l.pin_to(Anchor::Center) + l.text("...", centered_text({...}))
//
// Namespace: chronon3d::content::text_placement

#include <chronon3d/core/composition/composition_registry.hpp>

namespace chronon3d::content::text_placement {

/// Register all text-placement test compositions into the registry.
/// Called from content/register_content_modules.cpp.
void register_text_placement_compositions(CompositionRegistry& registry);

} // namespace chronon3d::content::text_placement
