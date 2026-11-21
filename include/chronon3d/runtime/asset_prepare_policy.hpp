#pragma once

#include <cstdlib>

namespace chronon3d::runtime::detail {

// Diagnostic override for same-payload serial/parallel asset-prepare A/Bs.
// The production default keeps the parallel path; set the variable before
// starting a process to force both image decode and runtime downscale serial.
[[nodiscard]] inline bool asset_prepare_serial_override_enabled() noexcept {
    static const bool enabled = std::getenv("CHRONON3D_ASSET_PREPARE_SERIAL") != nullptr;
    return enabled;
}

} // namespace chronon3d::runtime::detail
