#include "frame_delta_compiler.hpp"

#include "camera_change_policy.hpp"

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

FrameDelta compile_layer_delta(
    Frame frame,
    const std::unordered_map<std::string, LayerBBoxState>& current,
    const std::unordered_map<std::string, LayerBBoxState>& previous,
    bool camera_changed_value,
    int width,
    int height,
    const raster::TileGrid* tile_grid,
    const FrameDeltaCompileOptions& options) {
    FrameDelta result;
    result.frame = frame;
    result.camera_changed = camera_changed_value;
    if (tile_grid) {
        result.dirty_tiles.emplace(*tile_grid);
    }

    bool has_dirty = false;
    raster::BBox union_dirty{0, 0, 0, 0};

    const auto add_dirty_bbox = [&](raster::BBox bbox, double spread = 0.0) {
        if (bbox.is_empty()) return;
        if (spread > 0.0) {
            const auto amount = static_cast<int>(std::ceil(spread));
            bbox.x0 -= amount;
            bbox.y0 -= amount;
            bbox.x1 += amount;
            bbox.y1 += amount;
        }
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
            add_dirty_bbox(curr.bbox, options.spatial_spread
                ? options.spatial_spread(name) : 0.0);
            continue;
        }

        const auto& prev = prev_it->second;
        const bool visibility_changed = curr.visible != prev.visible;
        const bool transform_changed = !same_matrix(curr.world_matrix, prev.world_matrix);
        const bool bounds_changed = !same_bbox(curr.bbox, prev.bbox);
        const bool position_delta = position_changed(curr.world_matrix, prev.world_matrix);
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
            curr.opacity != prev.opacity ||
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
        if (camera_changed_value &&
            (curr.uses_2_5d_projection || prev.uses_2_5d_projection)) {
            mask |= LayerCamera;
        }
        if (mask == 0) continue;

        result.changes.push_back(LayerDelta{name, mask, prev.bbox, curr.bbox});
        if (visibility_changed) {
            add_dirty_bbox(curr.visible ? curr.bbox : prev.bbox,
                           options.spatial_spread ? options.spatial_spread(name) : 0.0);
        } else if (geometry_changed) {
            const auto spread = options.spatial_spread
                ? options.spatial_spread(name) : 0.0;
            add_dirty_bbox(curr.bbox, spread);
            add_dirty_bbox(prev.bbox, spread);
        } else if (content_changed && curr.visible) {
            add_dirty_bbox(curr.bbox, options.spatial_spread
                ? options.spatial_spread(name) : 0.0);
        }
    }

    for (const auto& [name, prev] : previous) {
        if (current.find(name) == current.end()) {
            std::uint32_t mask = LayerRemoved | LayerStructure;
            mark_present_semantics(prev, mask);
            result.changes.push_back(LayerDelta{name, mask, prev.bbox, raster::BBox{}});
            add_dirty_bbox(prev.bbox, options.spatial_spread
                ? options.spatial_spread(name) : 0.0);
        }
    }

    std::sort(result.changes.begin(), result.changes.end(),
              [](const LayerDelta& a, const LayerDelta& b) {
                  return a.instance_id < b.instance_id;
              });

    auto full_frame = raster::BBox{0, 0, width, height};
    if (options.force_full_frame) {
        result.dirty_bounds = full_frame;
        result.full_frame_dirty = true;
        if (result.dirty_tiles) result.dirty_tiles->mark_all();
    } else if (options.dirty_bounds_override.has_value()) {
        auto override_bounds = *options.dirty_bounds_override;
        override_bounds.clip_to(width, height);
        result.dirty_bounds = override_bounds;
        result.full_frame_dirty = override_bounds.x0 == 0 && override_bounds.y0 == 0 &&
            override_bounds.x1 == width && override_bounds.y1 == height;
        if (result.dirty_tiles) {
            result.dirty_tiles->clear();
            if (result.full_frame_dirty) result.dirty_tiles->mark_all();
            else result.dirty_tiles->mark_bbox(*tile_grid, override_bounds);
        }
    } else if (has_dirty) {
        result.dirty_bounds = union_dirty;
        const auto frame_area = static_cast<double>(width) * static_cast<double>(height);
        const auto dirty_area = static_cast<double>(union_dirty.x1 - union_dirty.x0) *
            static_cast<double>(union_dirty.y1 - union_dirty.y0);
        if (options.full_frame_threshold > 0.0 && frame_area > 0.0 &&
            dirty_area > frame_area * options.full_frame_threshold) {
            result.dirty_bounds = full_frame;
            result.full_frame_dirty = true;
            if (result.dirty_tiles) result.dirty_tiles->mark_all();
        }
    } else {
        result.dirty_bounds = raster::BBox{0, 0, 0, 0};
    }

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
    if (result.camera_changed && !options.dirty_bounds_override.has_value()) {
        result.dirty_bounds = full_frame;
        result.full_frame_dirty = true;
        if (result.dirty_tiles) result.dirty_tiles->mark_all();
    }
    return result;
}

FrameReuseEligibility compute_reuse_eligibility(
    const FrameStateSnapshot& previous,
    const FrameStateSnapshot& current) {
    FrameReuseEligibility result;
    result.camera_unchanged = !camera_changed(
        current.camera,
        previous.camera_valid ? &previous.camera : nullptr,
        previous.camera_valid);

    if (!previous.fingerprints_valid || !current.fingerprints_valid) {
        result.reason = "fingerprints_unavailable";
        return result;
    }

    const bool same_frame = previous.frame.integral() == current.frame.integral();
    const bool sequential_frame =
        current.frame.integral() == previous.frame.integral() + 1;
    const bool frame_eligible = same_frame || sequential_frame;
    const bool projected = previous.has_projected_surface ||
                           current.has_projected_surface;
    const bool combined_unchanged =
        current.fingerprints.combined_fp != 0 &&
        previous.fingerprints.combined_fp == current.fingerprints.combined_fp;
    const bool static_unchanged =
        previous.fingerprints.static_fp != 0 &&
        previous.fingerprints.static_fp == current.fingerprints.static_fp;
    const bool active_at_unchanged =
        current.fingerprints.active_at_fp != 0 &&
        previous.fingerprints.active_at_fp == current.fingerprints.active_at_fp;

    result.structure_unchanged =
        previous.fingerprints.static_fp != 0 &&
        previous.fingerprints.structure_fp == current.fingerprints.structure_fp;

    if (projected) {
        result.reason = "projected_surface";
    } else if (!current.has_previous_surface) {
        result.reason = "missing_previous_surface";
    } else if (!frame_eligible) {
        result.reason = "non_sequential_previous_frame";
    } else if (!result.camera_unchanged) {
        result.reason = "camera_changed";
    } else if (!combined_unchanged) {
        result.reason = "combined_fingerprint_changed";
    } else {
        // An unchanged scene fingerprint does not imply unchanged pixels:
        // transition/animation state is sampled by frame.  Sequential-frame
        // reuse is therefore safe only after the scene has been classified
        // as static; same-frame reuse remains valid for repeated evaluation.
        result.resolved_scene_reuse = same_frame ||
            (current.scene_is_static && sequential_frame);
        result.reason = {};
    }

    const bool static_frame_eligible = current.has_previous_surface &&
        current.layer_state_complete && previous.layer_state_complete &&
        (same_frame || (current.scene_is_static && sequential_frame));
    result.static_scene_reuse = !projected && static_frame_eligible &&
        result.structure_unchanged && result.camera_unchanged &&
        active_at_unchanged && static_unchanged;

    if (result.resolved_scene_reuse || result.static_scene_reuse) {
        result.reason = {};
    } else if (result.reason.empty()) {
        if (!result.structure_unchanged) {
            result.reason = "structure_changed";
        } else if (!active_at_unchanged) {
            result.reason = "active_at_changed";
        } else if (!static_unchanged) {
            result.reason = "static_fingerprint_changed";
        } else if (!current.scene_is_static) {
            result.reason = "not_static_scene";
        }
    }

    return result;
}

} // namespace

bool FrameDeltaCompiler::camera_unchanged(
    const Camera2_5D& current,
    const Camera2_5D* previous,
    bool previous_valid) {
    return !camera_changed(current, previous, previous_valid);
}

FrameDelta FrameDeltaCompiler::compile(
    Frame frame,
    const std::unordered_map<std::string, LayerBBoxState>& current,
    const std::unordered_map<std::string, LayerBBoxState>& previous,
    bool camera_changed_value,
    int width,
    int height,
    const raster::TileGrid* tile_grid,
    const FrameDeltaCompileOptions& options) {
    return compile_layer_delta(
        frame, current, previous, camera_changed_value,
        width, height, tile_grid, options);
}

FrameDelta FrameDeltaCompiler::compile_state(
    const FrameStateSnapshot& previous,
    const FrameStateSnapshot& current,
    int width,
    int height,
    const raster::TileGrid* tile_grid,
    const FrameDeltaCompileOptions& options) {
    const bool changed = camera_changed(
        current.camera,
        previous.camera_valid ? &previous.camera : nullptr,
        previous.camera_valid);
    FrameDelta result = compile_layer_delta(
        current.frame, current.layers, previous.layers, changed,
        width, height, tile_grid, options);
    result.reuse = compute_reuse_eligibility(previous, current);
    if (!result.changes.empty() || changed) {
        result.reuse.resolved_scene_reuse = false;
        result.reuse.static_scene_reuse = false;
        result.reuse.reason = changed ? "camera_changed" : "layer_delta_present";
    }
    result.camera_changed = changed;
    result.scene_changed = result.scene_changed || changed;
    return result;
}

} // namespace chronon3d::graph::detail
