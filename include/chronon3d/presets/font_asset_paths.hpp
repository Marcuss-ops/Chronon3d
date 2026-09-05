#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/presets/font_asset_paths.hpp
//
// Canonical font asset-relative path constants for presets and defaults.
// ═════════════════════════════════════════════════════════════════════════════
//
// Cat-3 single authority: preset/shape structs must reference these
// constants instead of hard-coding asset-relative font path literals.
// The arch rule `asset_lookup_hardcoded_process_cwd` forbids scattered
// "assets/fonts/..." literals in include/ and src/ — this header is the
// sole allow-listed site. Paths are asset-relative and resolve only
// through the canonical AssetResolver / set_assets_root() wiring (never
// the process CWD).
// ═════════════════════════════════════════════════════════════════════════════

#include <string>
#include <string_view>

namespace chronon3d::presets {

inline constexpr std::string_view kInterBoldFontAsset     = "assets/fonts/Inter-Bold.ttf";
inline constexpr std::string_view kInterRegularFontAsset  = "assets/fonts/Inter-Regular.ttf";
inline constexpr std::string_view kPoppinsBoldFontAsset   = "assets/fonts/Poppins-Bold.ttf";
inline constexpr std::string_view kPoppinsRegularFontAsset = "assets/fonts/Poppins-Regular.ttf";

inline std::string inter_bold_font_path()     { return std::string(kInterBoldFontAsset); }
inline std::string inter_regular_font_path()  { return std::string(kInterRegularFontAsset); }
inline std::string poppins_bold_font_path()   { return std::string(kPoppinsBoldFontAsset); }
inline std::string poppins_regular_font_path() { return std::string(kPoppinsRegularFontAsset); }

} // namespace chronon3d::presets
