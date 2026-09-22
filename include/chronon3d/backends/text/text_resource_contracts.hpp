#pragma once

// Lightweight value/forward-declaration surface shared by text cache
// consumers. TextRenderResources remains the single runtime owner; this
// header only keeps cache value contracts out of the font/scratch façade.

#include <blend2d.h>

#include <cstdint>

namespace chronon3d {

struct TextRasterization {
    BLImage image;
};

namespace assets { class AssetResolver; }
namespace detail { struct TextRasterCache; class GlyphAtlasCache; }
struct GlyphAtlasEntry;
struct GlyphAtlasStats;
struct PlacedGlyphRun;

} // namespace chronon3d
