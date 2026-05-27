#pragma once

#include <chronon3d/assets/render_preflight.hpp>

namespace chronon3d {

// Internal helpers shared between render_preflight_helpers.cpp and
// render_preflight_validate.cpp — not part of the public API.
std::string severity_label(PreflightSeverity s);
std::string asset_type_label(PreflightAssetType t);

} // namespace chronon3d
