#pragma once

// Region policy for the RGBA surface -> NV12 encoder boundary.
//
// The frame graph composites the canonical RGBA surface; the boundary then
// converts it into the encoder's NV12 planes. Converting only a sub-region is
// a genuine optimization, but it is sound ONLY when every region left
// untouched already holds the previous frame's pixels in the DESTINATION.
//
// That fact is not free, and it is not implied by the graph's dirty set:
//
//   * a persistent surface the renderer patches in place satisfies it;
//   * a pooled encoder frame obtained fresh from the hwframe pool does not
//     satisfy it by itself. Its bytes outside the converted region are whatever
//     the recycled buffer happened to hold. A per-buffer continuity tracker can
//     establish the proof after a known conversion and bounded damage history.
//
// Observed failure this contract prevents: multi-layer clips (background +
// foreground + watermark) rendered every layer, yet the encoded frame was a
// flat field / a single base layer, because the region set handed to the
// boundary came from the SOFTWARE renderer's predicted dirty rect instead of
// the pixels the GPU graph actually wrote.
//
// The helper below is therefore the single authority for "which launches does
// the boundary perform": callers pass the regions they want, continuity is a
// separate and explicit fact they must prove. A partial request that cannot be
// proven is not silently honoured — it is reported and widened to the frame.

#include <chronon3d/math/raster_utils.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::media::video {

/// The launches the boundary will perform for one frame.
struct Nv12ConversionPlan {
    /// Regions to convert, in destination pixel space, clamped to the frame
    /// and aligned to the 2x2 chroma grid. Never empty.
    std::vector<raster::BBox> regions;

    /// True when the whole frame is converted (nothing was requested, or a
    /// partial request was not admissible).
    bool full_frame{true};

    /// True when a non-empty request was widened to the full frame because the
    /// caller could not prove the destination retains the previous frame.
    /// The request is safely widened to a whole-frame conversion.
    bool partial_rejected{false};
};

/// Counts the Y and UV plane bytes written by the conversion kernels for this
/// plan. Overlapping regions are counted more than once because each launch
/// rewrites those samples. The plan's regions are expected to be clamped and
/// aligned by `plan_nv12_conversion`.
[[nodiscard]] inline std::uint64_t nv12_conversion_written_bytes(
    const Nv12ConversionPlan& plan) noexcept {
    std::uint64_t bytes = 0;
    for (const auto& region : plan.regions) {
        const auto width = static_cast<std::uint64_t>(
            std::max(0, region.x1 - region.x0));
        const auto height = static_cast<std::uint64_t>(
            std::max(0, region.y1 - region.y0));
        const auto luma_bytes = width * height;
        const auto chroma_width = (width + 1) / 2;
        const auto chroma_height = (height + 1) / 2;
        bytes += luma_bytes + chroma_width * chroma_height * 2;
    }
    return bytes;
}

/// Normalizes a requested region set into admissible conversion launches.
///
/// `destination_retains_previous_frame` is the caller's proof that pixels
/// outside the requested regions are already correct for this frame. Absent
/// that proof the whole frame is returned, so the boundary degrades to the
/// always-correct conversion instead of publishing a torn frame.
[[nodiscard]] inline Nv12ConversionPlan plan_nv12_conversion(
    std::span<const raster::BBox> requested, std::uint32_t width,
    std::uint32_t height,
    bool destination_retains_previous_frame = false) {
    const auto w = static_cast<int>(width);
    const auto h = static_cast<int>(height);

    Nv12ConversionPlan plan;
    if (requested.empty() || !destination_retains_previous_frame ||
        w <= 0 || h <= 0) {
        plan.partial_rejected = !requested.empty();
        plan.regions.push_back(raster::BBox{0, 0, w, h});
        return plan;
    }

    for (raster::BBox region : requested) {
        // Chroma is subsampled 2x2: origins round down, ends round up, and the
        // frame edge wins over the alignment so an odd-sized canvas still has
        // its last column/row converted.
        region.x0 = std::max(0, region.x0) & ~1;
        region.y0 = std::max(0, region.y0) & ~1;
        region.x1 = std::min(w, (region.x1 + 1) & ~1);
        region.y1 = std::min(h, (region.y1 + 1) & ~1);
        if (!region.is_empty()) plan.regions.push_back(region);
    }

    if (plan.regions.empty()) {
        plan.regions.push_back(raster::BBox{0, 0, w, h});
        return plan;
    }
    plan.full_frame = false;
    return plan;
}

} // namespace chronon3d::media::video
