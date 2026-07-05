#pragma once

// =============================================================================
// content/ae_parity/register_ae_parity_compositions.hpp
//
// Declares register_ae_parity_compositions() — registers the 10 AE parity
// camera visual comparison scenes as CLI-renderable compositions.
// =============================================================================

namespace chronon3d {

class CompositionRegistry;

namespace content::ae_parity {

/// Register AE_CAM_01 through AE_CAM_10 as compositions in the registry.
/// Each scene isolates one AE parity camera category from CAMERA_FEATURE_MATRIX.md.
void register_ae_parity_compositions(CompositionRegistry& registry);

} // namespace content::ae_parity
} // namespace chronon3d
