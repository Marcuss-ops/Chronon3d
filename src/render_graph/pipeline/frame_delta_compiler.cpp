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

bool position_changed(const Mat4& a, const Mat4& b) noexcept {
    return a[3][0] != b[3][0] || a[3][1] != b[3][1] || a[3][2] != b[3][2];
}

bool semantic_changed(
    const LayerBBoxState& current,
    const LayerBBoxState& previous,
    std::uint32_t kind,
    std::uint64_t current_hash,
    std::uint64_t previous_hash) noexcept {
    if (!current.semantic_fingerprints_valid ||
        !previous.semantic_fingerprints_valid) {
        return false;
    }
    const bool current_present = (current.semantic_presence & kind) != 0;
    const bool previous_present = (previous.semantic_presence & kind) != 0;
    return current_present != previous_present || current_hash != previous_hash;
}

void mark_present_semantics(const LayerBBoxState& state, std::uint32_t& mask) noexcept {
    if (state.semantic_presence & SemanticText) mask |= LayerText;
    if (state.semantic_presence & SemanticColor) mask |= LayerColor;
    if (state.semantic_presence & SemanticImage) mask |= LayerImage;
    if (state.semantic_presence & SemanticEffects) mask |= LayerEffects;
    if (state.semantic_presence & SemanticVideoSource) mask |= LayerVideoSource;
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
    result.camera_changed = camera_changed;
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
            std::uint32_t mask = LayerAdded | LayerStructure;
            mark_present_semantics(curr, mask);
            result.changes.push_back(LayerDelta{name, mask, raster::BBox{}, curr.bbox});
            add_dirty_bbox(curr.bbox);
            continue;
        }

        const auto& prev = prev_it->second;
        const bool visibility_changed = curr.visible != prev.visible;
        const bool transform_changed = !same_matrix(curr.world_matrix, prev.world_matrix);
        const bool bounds_changed = !same_bbox(curr.bbox, prev.bbox);
        const bool position_delta = position_changed(curr.world_matrix, prev.world_matrix);
        // LayerGeometry remains the broad, backwards-compatible transform /
        // footprint bit. LayerPosition is the new precise sub-classification.
        const bool geometry_changed = transform_changed || bounds_changed;
        const bool opacity_changed = curr.opacity != prev.opacity;

        const bool text_changed = semantic_changed(
            curr, prev, SemanticText, curr.text_hash, prev.text_hash);
        const bool color_changed = semantic_changed(
            curr, prev, SemanticColor, curr.color_hash, prev.color_hash);
        const bool image_changed = semantic_changed(
            curr, prev, SemanticImage, curr.image_hash, prev.image_hash);
        const bool effects_changed = semantic_changed(
            curr, prev, SemanticEffects, curr.effects_hash, prev.effects_hash);
        const bool video_source_changed = semantic_changed(
            curr, prev, SemanticVideoSource,
            curr.video_source_hash, prev.video_source_hash);
        const bool semantic_content_changed = text_changed || color_changed ||
            image_changed || effects_changed || video_source_changed;
        const bool structure_changed =
            curr.semantic_fingerprints_valid && prev.semantic_fingerprints_valid &&
            curr.structure_hash != prev.structure_hash;
        const bool content_changed =
            !curr.cache_static || curr.opacity != prev.opacity ||
            curr.content_hash != prev.content_hash || semantic_content_changed ||
            structure_changed;

        std::uint32_t mask = 0;
        if (visibility_changed) mask |= LayerVisibility;
        if (geometry_changed) mask |= LayerGeometry;
        if (content_changed) mask |= LayerContent;
        if (structure_changed) mask |= LayerStructure;
        if (position_delta) mask |= LayerPosition;
        if (opacity_changed) mask |= LayerOpacity;
        if (text_changed) mask |= LayerText;
        if (color_changed) mask |= LayerColor;
        if (image_changed) mask |= LayerImage;
        if (effects_changed) mask |= LayerEffects;
        if (video_source_changed) mask |= LayerVideoSource;
        if (camera_changed &&
            (curr.uses_2_5d_projection || prev.uses_2_5d_projection)) {
            mask |= LayerCamera;
        }
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
            std::uint32_t mask = LayerRemoved | LayerStructure;
            mark_present_semantics(prev, mask);
            result.changes.push_back(LayerDelta{name, mask, prev.bbox, raster::BBox{}});
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

    for (const auto& change : result.changes) {
        const auto mask = change.change_mask;
        result.structure_changed |= (mask & (LayerAdded | LayerRemoved | LayerStructure)) != 0;
        result.geometry_changed |= (mask & LayerGeometry) != 0;
        result.content_changed |= (mask & LayerContent) != 0;
        result.visibility_changed |= (mask & LayerVisibility) != 0;
        result.position_changed |= (mask & LayerPosition) != 0;
        result.opacity_changed |= (mask & LayerOpacity) != 0;
        result.text_changed |= (mask & LayerText) != 0;
        result.color_changed |= (mask & LayerColor) != 0;
        result.image_changed |= (mask & LayerImage) != 0;
        result.effects_changed |= (mask & LayerEffects) != 0;
        result.video_source_changed |= (mask & LayerVideoSource) != 0;
    }
    result.scene_changed = result.camera_changed || !result.changes.empty();
    return result;
}

} // namespace chronon3d::graph::detail
