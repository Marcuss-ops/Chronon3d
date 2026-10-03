#pragma once

// Deterministic CPU propagation of an authored/segmented soft mask between
// adjacent video frames. Dense pyramidal Lucas–Kanade flow carries the mask;
// frame alpha weights visible support so transparent RGB cannot steer
// tracking. Forward/backward disagreement and local texture produce an explicit
// confidence map so callers can request a new segmentation seed at occlusions.

#include <chronon3d/math/color.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace chronon3d::media::tracking {

struct TrackingFrameView {
    std::span<const Color> pixels{};
    std::uint32_t width{0};
    std::uint32_t height{0};
    // Zero means tightly packed. Framebuffer callers can pass allocated_width.
    std::uint32_t row_stride{0};
    bool premultiplied_alpha{true};
};

struct SoftMaskView {
    std::span<const float> alpha{};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t row_stride{0};
    // Optional confidence from an earlier propagation step. Output confidence
    // is bounded by both the current flow and this history. Authored anchors
    // should omit it to begin a fresh tracking interval.
    std::span<const float> confidence{};
    std::uint32_t confidence_row_stride{0};
};

struct TemporalFlowOptions {
    std::uint32_t pyramid_levels{4};
    std::uint32_t window_radius{3};
    std::uint32_t iterations_per_level{5};
    float minimum_eigenvalue{1.0e-4f};
    float minimum_confidence{0.12f};
    float forward_backward_tolerance{1.5f};
    float maximum_motion_per_frame{128.0f};
    // Normalizes the photometric residual used for confidence. Linear PQ
    // decoder values use 1.0 == 100 nits, so HDR10 inputs set this to 100.
    float photometric_scale{1.0f};
    std::uint64_t maximum_work_units{2'000'000'000};
};

struct PropagatedMask {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::vector<float> alpha{};
    std::vector<float> confidence{};
    std::uint64_t rejected_low_texture{0};
    std::uint64_t rejected_occlusion{0};
    std::uint64_t rejected_large_motion{0};
    std::uint64_t foreground_samples{0};
    std::uint64_t rejected_foreground_samples{0};
};

struct MaskAnchor {
    std::size_t frame_index{0};
    SoftMaskView mask{};
};

struct GuidedMaskRefinementOptions {
    std::uint32_t radius{4};
    float epsilon{1.0e-3f};
};

class TemporalMaskPropagator final {
public:
    /// Conservative work estimate for one adjacent-frame propagation pair.
    [[nodiscard]] static std::uint64_t estimate_pair_work_units(
        std::uint32_t width, std::uint32_t height,
        TemporalFlowOptions options = {}) noexcept;

    /// Refine soft-mask boundaries using a grayscale guided filter over the
    /// corresponding linear-light frame. Local statistics are weighted by
    /// frame alpha; fully transparent pixels cannot guide or receive matte.
    /// The authored/flow mask is the input signal; the source mask is unchanged.
    [[nodiscard]] static std::vector<float> refine_mask_edges(
        TrackingFrameView frame,
        SoftMaskView mask,
        GuidedMaskRefinementOptions options = {});

    /// Propagate a mask from `source_frame` into `target_frame`.
    ///
    /// The frames and mask must have matching dimensions. Source pixels are
    /// linear-light RGBA; mask alpha is clamped to [0,1]. Optional source
    /// confidence carries tracking history, preventing a failed region from
    /// regaining certainty on later frames. Output confidence is zero where
    /// tracking failed and mask alpha is zero there, so stale foreground is
    /// never silently extended through an occlusion. The caller should re-seed
    /// those areas with a new segmentation/path mask.
    [[nodiscard]] static PropagatedMask propagate(
        TrackingFrameView source_frame,
        TrackingFrameView target_frame,
        SoftMaskView source_mask,
        TemporalFlowOptions options = {});

    /// Propagate one seed mask through an ordered clip in both directions.
    /// Each interval is solved from its neighboring source/target frames; the
    /// seed frame itself is returned with confidence 1 for finite mask pixels.
    [[nodiscard]] static std::vector<PropagatedMask> propagate_sequence(
        std::span<const TrackingFrameView> frames,
        std::size_t seed_frame,
        SoftMaskView seed_mask,
        TemporalFlowOptions options = {});

    /// Propagate through an ordered clip using authored correction mattes at
    /// strictly increasing frame indices. Intervals are propagated from both
    /// anchors and confidence-weighted together; anchor mattes are preserved.
    [[nodiscard]] static std::vector<PropagatedMask> propagate_anchored_sequence(
        std::span<const TrackingFrameView> frames,
        std::span<const MaskAnchor> anchors,
        TemporalFlowOptions options = {});
};

} // namespace chronon3d::media::tracking
