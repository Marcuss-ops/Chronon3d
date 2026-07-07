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

/// Create an AssetResolver mounted at the composition's assets root
/// (or CWD if empty).  Scoped to the caller — no global mutation.
inline chronon3d::assets::AssetResolver make_cli_resolver(
    const std::string& assets_root) {
    chronon3d::assets::AssetResolver resolver;
    if (!assets_root.empty()) {
        resolver.mount(std::filesystem::path(assets_root));
    } else {
        resolver.mount(std::filesystem::current_path());
    }
    return resolver;
}

} // namespace chronon3d::cli
