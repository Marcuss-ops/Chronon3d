// ═══════════════════════════════════════════════════════════════════════════
// P1-9 — `src/backends/text/glyph_atlas.cpp` is now a NO-OP stub.
//
// The cache state (LruCache, capacity, atomic first-call-wins,
// shared_mutex) and the 4 free function implementations that USED to
// live here have been migrated to `TextRenderResources` member methods
// (see `text_render_resources.{hpp,cpp}`).  The 4 free function
// declarations in `include/chronon3d/text/glyph_atlas.hpp` have also
// been deleted (Cat-3 anti-duplication: thin wrappers added no value
// beyond what the member methods provide).
//
// This file is kept as a no-op stub to avoid touching CMakeLists.txt
// (the source file is still listed in the `chronon3d_backend_text`
// target).  If the file is later removed from CMake, this stub can be
// deleted entirely.
//
// Migration map:
//   glyph_atlas_lookup         → TextRenderResources::lookup_glyph_atlas
//   glyph_atlas_store          → TextRenderResources::store_glyph_atlas
//   glyph_atlas_stats          → TextRenderResources::glyph_atlas_stats
//   glyph_atlas_store_from_placed_run → TextRenderResources::store_glyph_atlas_from_placed_run
//   set_glyph_atlas_capacity   → TextRenderResources::set_glyph_atlas_capacity
//   glyph_atlas_clear          → TextRenderResources::clear_glyph_atlas
//   get_glyph_atlas            → DELETED (PIMPL is now a private member of TextRenderResources)
//   get_glyph_atlas_mutex      → DELETED (internal shared_mutex is now a PIMPL member)
// ═══════════════════════════════════════════════════════════════════════════

// Suppress -Wunused-variable for the include below (the header is kept
// for the value types GlyphAtlasEntry + GlyphAtlasStats even though
// this TU no longer references them directly).
#include <chronon3d/text/glyph_atlas.hpp>

namespace chronon3d {
// P1-9: all free functions deleted.  The atlas now lives per-instance
// on `TextRenderResources` (see text_render_resources.cpp::GlyphAtlasCache
// PIMPL).  This namespace is intentionally empty.
} // namespace chronon3d
