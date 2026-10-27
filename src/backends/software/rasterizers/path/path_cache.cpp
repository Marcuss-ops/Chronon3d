#include "path_cache.hpp"
#include "path_geometry.hpp"

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
