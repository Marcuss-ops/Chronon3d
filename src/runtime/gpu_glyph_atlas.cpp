#include <chronon3d/runtime/gpu_glyph_atlas.hpp>

#include <chronon3d/assets/prepared_asset_manifest.hpp>
#include <chronon3d/render_graph/render_backend.hpp>

#include <algorithm>
#include <cstring>
#include <sstream>

namespace chronon3d::runtime {

#include "gpu_glyph_atlas_core.inc"
#include "gpu_styled_glyph_cache.inc"

} // namespace chronon3d::runtime
