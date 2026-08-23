#include "frame_delta_compiler.hpp"

#include <algorithm>

namespace chronon3d::graph::detail {
namespace {

bool same_bbox(const raster::BBox& a, const raster::BBox& b) noexcept {
    return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}

bool same_matrix(const Mat4& a, const Mat4& b) noexcept {
    return a == b;
}

} // namespace

FrameDelta FrameDeltaCompiler::compile(
    Frame frame,
    const std::unordered_map<std::string, LayerBBoxState>& current,
    const std::unordered_map<std::string, LayerBBoxState>& previous,
    bool camera_changed,
    int width,
    int height,
    const raster::TileGrid* tile_grid) {
    FrameDelta result;
    result.frame = frame;
    if (tile_grid) {
        result.dirty_tiles.emplace(*tile_grid);
    }

    bool has_dirty = false;
    raster::BBox union_dirty{0, 0, 0, 0};

    const auto add_dirty_bbox = [&](raster::BBox bbox) {
        if (bbox.is_empty()) return;
        bbox.clip_to(width, height);
        if (bbox.is_empty()) return;
        if (!has_dirty) {
            union_dirty = bbox;
            has_dirty = true;
        } else {
            union_dirty.x0 = std::min(union_dirty.x0, bbox.x0);
            union_dirty.y0 = std::min(union_dirty.y0, bbox.y0);
            union_dirty.x1 = std::max(union_dirty.x1, bbox.x1);
            union_dirty.y1 = std::max(union_dirty.y1, bbox.y1);
        }
        if (result.dirty_tiles) {
            result.dirty_tiles->mark_bbox(*tile_grid, bbox);
        }
    };

    for (const auto& [name, curr] : current) {
        const auto prev_it = previous.find(name);
        if (prev_it == previous.end()) {
            result.changes.push_back(LayerDelta{
                name, LayerAdded, raster::BBox{}, curr.bbox});
            add_dirty_bbox(curr.bbox);
            continue;
        }

        const auto& prev = prev_it->second;
        const bool visibility_changed = curr.visible != prev.visible;
        const bool geometry_changed =
            (camera_changed && curr.uses_2_5d_projection) ||
            !same_matrix(curr.world_matrix, prev.world_matrix);
        const bool content_changed =
            !curr.cache_static || curr.opacity != prev.opacity ||
            curr.content_hash != prev.content_hash;

        std::uint32_t mask = 0;
        if (visibility_changed) mask |= LayerVisibility;
        if (geometry_changed) mask |= LayerGeometry;
        if (content_changed) mask |= LayerContent;
        if (mask == 0) continue;

        result.changes.push_back(LayerDelta{name, mask, prev.bbox, curr.bbox});
        if (visibility_changed) {
            add_dirty_bbox(curr.visible ? curr.bbox : prev.bbox);
        } else if (geometry_changed) {
            add_dirty_bbox(curr.bbox);
            add_dirty_bbox(prev.bbox);
        } else if (content_changed && curr.visible) {
            add_dirty_bbox(curr.bbox);
        }
    }

    for (const auto& [name, prev] : previous) {
        if (current.find(name) == current.end()) {
            result.changes.push_back(LayerDelta{
                name, LayerRemoved, prev.bbox, raster::BBox{}});
            add_dirty_bbox(prev.bbox);
        }
    }

    // The input maps are unordered, but the delta is part of the execution
    // decision and must be stable for reproducible plans and telemetry.
    std::sort(result.changes.begin(), result.changes.end(),
              [](const LayerDelta& a, const LayerDelta& b) {
                  return a.instance_id < b.instance_id;
              });

    result.dirty_bounds = has_dirty
        ? std::optional<raster::BBox>{union_dirty}
        : std::optional<raster::BBox>{raster::BBox{0, 0, 0, 0}};
    return result;
}

} // namespace chronon3d::graph::detail
