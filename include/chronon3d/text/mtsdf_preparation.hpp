#pragma once

#include <chronon3d/text/mtsdf_glyph.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#include <memory>
#include <string_view>

namespace chronon3d::assets { class AssetResolver; }

namespace chronon3d::text {

/// One backend-neutral MTSDF preparation authority. The returned immutable
/// payload is shared by Vulkan and DirectYUV; each backend owns only its
/// residency/lowering. A failed resolve or glyph generation returns null and
/// never falls back to a backend-local font loader.
[[nodiscard]] std::shared_ptr<const MtsdfGlyphBitmap> prepare_mtsdf_glyph(
    const assets::AssetResolver& resolver,
    std::string_view font_path,
    std::uint32_t glyph_id,
    const MtsdfProfile& profile = kCanonicalMtsdfProfile);

} // namespace chronon3d::text
