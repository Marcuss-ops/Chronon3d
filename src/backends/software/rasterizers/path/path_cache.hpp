#pragma once

// PR2: path_cache is now a thin back-compat shim.
// The actual source-of-truth lives in path_geometry.hpp /
// path_geometry.cpp (single LRU cache, single world bbox, single
// hit_test).  This header re-exports the legacy names so existing
// callers do not need a churn commit.

#include "path_geometry.hpp"

namespace chronon3d::renderer {

// Legacy alias: was `CacheKey` in old path_cache.hpp.
using CacheKey = PathCacheKey;

// Legacy callers may still call flatten_path_cached expecting a copy.
// The function is preserved as a thin wrapper that dereferences the
// shared_ptr.  New code should use flatten_path_geometry_cached() and
// keep the SharedContours to avoid copying the vector.
inline std::vector<PathContour> flatten_path_cached(const PathShape& path) {
    auto shared = flatten_path_geometry_cached(path);
    if (!shared) return {};
    return *shared;
}

} // namespace chronon3d::renderer

// ── NOTE ─────────────────────────────────────────────────────────────
// The following legacy declarations have been REMOVED from this header
// and now live exclusively in path_geometry.hpp:
//
//   * struct PathContour                  → path_geometry.hpp
//   * using PathCacheKey / CacheKey       → path_geometry.hpp
//   * path_cache_hash_*  functions        → path_geometry.hpp
//
// The legacy `path_flatten_cache()` function-local static LRU and the
// `g_flatten_cache` global in path_cache.cpp were duplicate cache
// instances; both have been removed in favour of the single LRU in
// path_geometry.cpp.
