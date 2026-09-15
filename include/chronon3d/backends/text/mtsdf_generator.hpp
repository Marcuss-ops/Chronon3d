#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>
#include <chronon3d/text/mtsdf_glyph.hpp>
#include <chronon3d/text/mtsdf_profile.hpp>

#ifdef CHRONON3D_ENABLE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace chronon3d {

#ifdef CHRONON3D_ENABLE_TEXT

/// Per-face access token for MTSDF generation.
///
/// `FT_Face` carries mutable size/glyph state, so FreeType access must be
/// serialized. The lock that protects it belongs to whoever OWNS the face
/// (a face-cache entry or a local owner), not to the generator: passing the
/// owner's mutex makes the guard per-face, so unrelated fonts rasterize
/// concurrently instead of serializing behind one process-wide lock.
///
/// A guard without a mutex cannot prove which lock protects the face, so it
/// fails closed (`generate_mtsdf_glyph` returns `nullopt`).
struct MtsdfFaceGuard {
    FT_Face     face{nullptr};
    std::mutex* mutex{nullptr};

    [[nodiscard]] bool valid() const noexcept {
        return face != nullptr && mutex != nullptr;
    }
};

/// Generate one canonical MTSDF glyph from a FreeType outline. The face must
/// already be loaded at `em_px`; callers normally obtain a cached face at the
/// atlas profile size (64 px), so the resulting key is independent of layout
/// font size. `face_guard.mutex` is held only for the FreeType-owned work.
[[nodiscard]] std::optional<MtsdfGlyphBitmap> generate_mtsdf_glyph(
    MtsdfFaceGuard face_guard,
    std::uint32_t glyph_id,
    std::uint16_t em_px = text::kCanonicalMtsdfProfile.em_px,
    std::uint16_t distance_range_px = text::kCanonicalMtsdfProfile.distance_range_px,
    std::uint16_t padding_px = text::kCanonicalMtsdfProfile.padding_px);

#endif

} // namespace chronon3d
