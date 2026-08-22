#pragma once

// Compatibility header.  GpuTextAtlasCache has been renamed to
// GpuStyledGlyphCache (see gpu_glyph_atlas.hpp).  This header exists
// so existing consumers compile without changes.
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>

namespace chronon3d::runtime {
using GpuTextAtlasCache = GpuStyledGlyphCache;
} // namespace chronon3d::runtime