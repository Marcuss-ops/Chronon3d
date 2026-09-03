// ============================================================================
// apps/chronon3d_cli/register_dev_compositions.cpp
//
// TICKET-CLI-ISOLATE-RUNTIME-DEV — DEV-only composition registration.
// ENTIRE FILE gated under #ifdef CHRONON3D_BUILD_CLI_DEV.  When
// CHRONON3D_BUILD_CLI_DEV=OFF (production default), this file is empty
// and the dev compositions are not registered.
//
// The visual/golden demo composition fleet (PipelineParityCanary,
// CameraTruth*, AE_CAM_01..10, ae_08..ae_14, glow A/B siblings,
// ChrononGlowFinalAE_NoGlow, AECameraTextParity) was retired together with
// the tests/visual + tests/golden + content-pack removal.  The file is kept
// as an (empty) hook point for future DEV-only compositions.
// ============================================================================

#ifdef CHRONON3D_BUILD_CLI_DEV

#include "register_compositions.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d {

void register_dev_compositions(CompositionRegistry& registry) {
    (void)registry;  // no DEV-only compositions remain after the cleanup
}

} // namespace chronon3d

#endif // CHRONON3D_BUILD_CLI_DEV
