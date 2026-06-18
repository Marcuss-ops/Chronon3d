#include "path_cache.hpp"
#include "path_geometry.hpp"

// PR2: path_cache.cpp used to host its own duplicate LRU cache + the
// real flatten work.  All of that has moved to path_geometry.cpp, so
// this file is now a thin shim that re-exports the legacy function
// names needed by callers expecting `vector<PathContour>` (e.g.
// path_rasterizer.hpp::flatten_path declaration).

namespace chronon3d::renderer {

// Legacy: was the cache-miss path.  Now copies from the unified cache.
std::vector<PathContour> flatten_to_contours(const PathShape& path) {
    auto shared = flatten_path_geometry_cached(path);
    if (!shared) return {};
    return *shared;
}

// Legacy: was an additional cache-miss passthrough.  Kept for ABI.
std::vector<PathContour> flatten_path(const PathShape& path) {
    return flatten_to_contours(path);
}

} // namespace chronon3d::renderer
