#pragma once

#include <chronon3d/core/dirty_tile_mask.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/dirty_history.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::graph::detail {

enum LayerDeltaChange : std::uint32_t {
    LayerAdded = 1u << 0,
    LayerRemoved = 1u << 1,
    LayerVisibility = 1u << 2,
    LayerGeometry = 1u << 3,
    LayerContent = 1u << 4,
};

struct LayerDelta {
    std::string instance_id;
    std::uint32_t change_mask{0};
    raster::BBox old_bounds{};
    raster::BBox new_bounds{};
};

/// Result of the canonical current-vs-previous layer analysis.
struct FrameDelta {
    Frame frame{};
    std::vector<LayerDelta> changes;
    std::optional<raster::BBox> dirty_bounds;
    std::optional<raster::DirtyTileMask> dirty_tiles;
};

/// Compiles layer state into one dirty-region decision for the frame.
///
/// This owns no history and performs no rendering.  The caller supplies the
/// canonical previous layer map from DirtyHistory, so exporters and backends
/// cannot grow independent delta/fingerprint logic.
class FrameDeltaCompiler final {
public:
    [[nodiscard]] static FrameDelta compile(
        Frame frame,
        const std::unordered_map<std::string, LayerBBoxState>& current,
        const std::unordered_map<std::string, LayerBBoxState>& previous,
        bool camera_changed,
        int width,
        int height,
        const raster::TileGrid* tile_grid = nullptr);
};

} // namespace chronon3d::graph::detail
