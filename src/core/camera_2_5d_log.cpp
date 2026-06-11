// ============================================================================
// camera_2_5d_log.cpp — Logging helpers for camera_2_5d_projection.hpp.
//
// Extracted from the inline header so it doesn't need to #include <spdlog/spdlog.h>.
// ============================================================================

#include <chronon3d/math/camera_2_5d_projection.hpp>
#include <spdlog/spdlog.h>

namespace chronon3d::detail {

void log_camera_projection_diagnostics(float depth, float determinant) {
    spdlog::info(
        "[diagnostics-3d] Compiled projection matrix. Depth={:.2f}, Det={:.4f} ({})",
        depth,
        determinant,
        determinant >= 0.0f ? "OK" : "FLIPPED/MIRRORED"
    );
}

} // namespace chronon3d::detail
