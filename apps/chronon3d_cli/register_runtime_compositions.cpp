// ============================================================================
// apps/chronon3d_cli/register_runtime_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — production runtime composition surface.
// Always called (production CLI + DEV CLI).
//
// Registers:
//   1. chronon3d::register_builtin_compositions(registry) — DarkGridBackground,
//      CameraImageClip (the canonical "always available" production surfaces).
//
// The ChrononGlowFinalAE / GlowCameraProductV1 families were removed with the
// content-pack externalization cleanup: the content/ compositions tree left
// the core repo and the tests/helpers glow factory was retired with the
// visual/golden test fleet.
// ============================================================================

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/composition/register_builtin_compositions.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {

void register_runtime_compositions(CompositionRegistry& registry) {
    // Built-in compositions (DarkGridBackground, CameraImageClip).
    chronon3d::register_builtin_compositions(registry);
}

} // namespace chronon3d
