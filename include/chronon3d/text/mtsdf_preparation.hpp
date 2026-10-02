#pragma once

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/text/mtsdf_glyph.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::text {

/// Canonical prepared-font content identity (the "PreparedFontIdentity" of the
/// MTSDF preparation authority).
///
/// ContentCache-family identity member: same bytes ⇒ same digest.  It resolves
/// the font through the canonical AssetResolver and memoises the SHA-256 per
/// (canonical path, byte size, mtime) stamp, so exactly one full read of a font
/// file happens per file version in the process — never once per glyph and
/// never once per texture lookup.
///
/// Returns nullopt only when `font_path` does not resolve inside the mounted
/// roots.  Every cache whose key depends on a font (the glyph LRU here and the
/// DirectYUV packed-texture lowering) MUST key on this identity rather than on
/// the font path, so a file whose bytes change mid-daemon can never satisfy a
/// hit with a payload built from the previous bytes.
[[nodiscard]] std::optional<assets::ContentDigest> prepared_font_content_digest(
    const assets::AssetResolver& resolver,
    std::string_view font_path);

/// One backend-neutral MTSDF preparation authority. The returned immutable
/// payload is shared by Vulkan and DirectYUV; each backend owns only its
/// residency/lowering. A failed resolve or glyph generation returns null and
/// never falls back to a backend-local font loader.
[[nodiscard]] std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile = kCanonicalMtsdfProfile);

/// Variation-aware overload; variation coordinates participate in the
/// preparation cache identity and are applied to the FreeType face that
/// generates the outline.
[[nodiscard]] std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile,
    std::string_view variation_axes);

} // namespace chronon3d::text
