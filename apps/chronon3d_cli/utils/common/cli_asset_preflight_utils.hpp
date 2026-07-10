#pragma once

// ── utils/common/cli_asset_preflight_utils.hpp
//
// Shared helper for CLI commands that need an AssetResolver mounted at
// the composition's assets root (or CWD if empty).
// Used by command_preflight and command_still.

#include <chronon3d/assets/asset_resolver.hpp>
#include <filesystem>
#include <string>

namespace chronon3d::cli {

/// Create an AssetResolver mounted at the composition's assets root.
/// F0.2 — the `current_path()` fallback has been removed.  Callers MUST
/// pass a non-empty assets_root (from `comp.assets_root()`).
inline chronon3d::assets::AssetResolver make_cli_resolver(
    const std::string& assets_root) {
    chronon3d::assets::AssetResolver resolver;
    if (!assets_root.empty()) {
        resolver.mount(std::filesystem::path(assets_root));
    }
    // F0.2: empty assets_root → unmounted resolver (no CWD fallback).
    // Callers that need asset resolution must supply a valid root.
    return resolver;
}

} // namespace chronon3d::cli
