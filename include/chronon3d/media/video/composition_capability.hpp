#pragma once

// =============================================================================
// composition_capability.hpp — Effect/compositor capability matrix (Z4)
//
// Single-authority declaration of which effect/layer classes the native
// DirectYUV composition path can composite WITHOUT leaving the NV12/P010
// domain (TICKET-ZERO-COPY-ZWAVES Z4, plan §5).  The DirectYuvProgram
// prepare/execute enforcement sites are the runtime consumers; this header
// is the *declared* matrix those sites mirror, and job-level resolvers use
// `resolve_direct_yuv_support()` to decide RequireDirectYuv (hard fail) vs
// Auto (explicit FullGraph fallback) BEFORE starting a job.  Never a silent
// runtime fallback and never a second capability authority.
//
// The matrix intentionally speaks feature vocabulary (2D transform, single
// shape node, GPU-resident RGBA/NV12 sample, Normal/Add blend) instead of
// scene-internal types, so it stays backend-neutral and unit-testable on
// CPU-only builds.
// =============================================================================

#include <chronon3d/compositor/blend_mode.hpp>

#include <cstdint>
#include <string_view>

namespace chronon3d::media::video {

/// Effect class of one authored layer, mapped from the layer's authored
/// semantics (background video vs overlay primitive vs full-frame effect).
enum class DirectYuvEffectClass : std::uint8_t {
    /// NVDEC NV12/P010 decoded background video (the DirectYUV base).
    VideoBackground = 0,
    /// Secondary decoded NV12/P010 video layer composited as an overlay.
    VideoOverlayNv12 = 1,
    /// GPU-resident RGBA image overlay (asset/watermark/logo).
    ImageOverlay = 2,
    /// Text/subtitle overlay rasterized to a GPU-resident RGBA texture.
    TextOverlay = 3,
    /// Rect/rounded-rect/gradient authored fill (not a sampled texture).
    BasicShapeOverlay = 4,
    /// Simple per-pixel color-matrix effect (BT.601/709 math inside kernel).
    ColorMatrixEffect = 5,
    /// Complex RGB effect that needs a full-frame RGBA intermediate
    /// (e.g. multi-tap blur, glow) — not fused into the NV12 writer.
    ComplexRgbaEffect = 6,
    /// Host/CPU-side effect — cannot run on a GPU-native lane.
    CpuEffect = 7,
    /// Mask, 2.5D projection, 3D transform, multi-node layer or an
    /// unimplemented blend mode: the fast path refuses these explicitly.
    UnsupportedFeature = 8,
};

/// Classification of an effect class for native YUV composition.
enum class DirectYuvSupport : std::uint8_t {
    /// The DirectYUV lane composites this natively (gates 3/4/6 stay 0).
    Certified = 0,
    /// Composition is only faithful through a full-frame RGBA pass; the
    /// DirectYUV lane must not approximate it.
    RequiresFullFrame = 1,
    /// The effect is host-bound; only the CPU lane can run it.
    CpuOnly = 2,
    /// The DirectYUV lane (and today's FullGraph autos) must refuse it.
    Unsupported = 3,
};

/// Feature-level query for one layer against the native-YUV fast path.
struct LayerYuvQuery {
    DirectYuvEffectClass effect{DirectYuvEffectClass::ImageOverlay};
    /// True when the overlay texture is (or will be) GPU-resident before the
    /// frame loop starts.  A host-only overlay source is not DirectYUV.
    bool gpu_resident{false};
    /// True when the layer uses only 2D transforms (no 3D rotation /
    /// position.z / anchor.z).  Mirrors DirectYuvProgram's is_2d_transform.
    bool two_d_transform{true};
    /// True when the layer carries an enabled alpha/luma mask.
    bool mask_enabled{false};
    /// True when the layer uses 2.5D projection.
    bool projection_enabled{false};
    /// True when the layer resolves to exactly one shape node per frame
    /// (the fast path composites one primitive per layer).
    bool single_shape_node{true};
    /// True only after the canonical TextRunShape has been validated and
    /// prepared as an MTSDF resource. TextOverlay is never admitted from a
    /// generic RGBA/host texture claim: the DirectYUV kernel needs the shared
    /// PreparedTextRun/MTSDF ABI.
    bool prepared_mtsdf{false};
    /// True only when the prepared run's remaining style semantics lower
    /// faithfully (no gradient paint, no premium material, no animated or
    /// dissolving document, and at most one enabled drop shadow, which the
    /// kernel resamples from the same prepared distance field). Resolved by
    /// the canonical text contract classifier; never asserted by a caller.
    bool text_style_representable{false};
    ::chronon3d::BlendMode blend{::chronon3d::BlendMode::Normal};
};

/// Classify one layer feature query for native YUV composition.
/// This table is the single authority the job resolver and the DirectYuv
/// enforcement sites consult.  Certified rows are exactly the effect set the
/// fused NV12 kernels implement (`composite_direct_nv12*`).
[[nodiscard]] inline DirectYuvSupport classify_direct_yuv(
    const LayerYuvQuery& query) noexcept {
    const bool structural = query.two_d_transform &&
                            !query.mask_enabled && !query.projection_enabled &&
                            query.single_shape_node;
    const bool additive_blend =
        query.blend == ::chronon3d::BlendMode::Normal ||
        query.blend == ::chronon3d::BlendMode::Add;

    switch (query.effect) {
        case DirectYuvEffectClass::VideoBackground:
            // The decoded NV12/P010 base is always composited in-domain.  A
            // non-2D/masked/projected/multi-node *background* is rejected by
            // the structural check below: the fast path has no transform
            // lattice to express it in.
            return structural ? DirectYuvSupport::Certified
                              : DirectYuvSupport::Unsupported;

        case DirectYuvEffectClass::ImageOverlay:
            // GPU-resident RGBA sampled straight into the NV12 writer.
            // Blend2D-rasterized image assets qualify; a host-only texture
            // never does (explicit, not silent).
            if (!query.gpu_resident) return DirectYuvSupport::RequiresFullFrame;
            return (structural && additive_blend)
                ? DirectYuvSupport::Certified
                : DirectYuvSupport::Unsupported;

        case DirectYuvEffectClass::TextOverlay:
            // Only the canonical prepared MTSDF resource is supported. A
            // generic GPU-resident text texture is not enough to establish
            // the field encoding and glyph/layout contract.
            if (!query.gpu_resident || !query.prepared_mtsdf) {
                return DirectYuvSupport::RequiresFullFrame;
            }
            // The prepared run must ALSO lower its style semantics faithfully;
            // the classifier resolved this from the canonical text contract, so
            // an unsupported shadow stack can never be approximated here.
            if (!query.text_style_representable) {
                return DirectYuvSupport::Unsupported;
            }
            return (structural && additive_blend)
                ? DirectYuvSupport::Certified
                : DirectYuvSupport::Unsupported;

        case DirectYuvEffectClass::VideoOverlayNv12:
            // The scene classifier knows how to describe a second NV12
            // source, but the current fused batch executor still accepts
            // only RGBA resources. Advertising this row as certified makes
            // a background+foreground clip reach the kernel and fail at
            // frame zero. Route it through the faithful full-frame graph
            // until the multi-source NV12 kernel is actually shipped.
            return DirectYuvSupport::RequiresFullFrame;

        case DirectYuvEffectClass::ColorMatrixEffect:
            return DirectYuvSupport::Certified;

        case DirectYuvEffectClass::BasicShapeOverlay:
        case DirectYuvEffectClass::ComplexRgbaEffect:
            // Faithful output requires a full-frame RGBA composition pass.
            return DirectYuvSupport::RequiresFullFrame;

        case DirectYuvEffectClass::CpuEffect:
            return DirectYuvSupport::CpuOnly;

        case DirectYuvEffectClass::UnsupportedFeature:
            return DirectYuvSupport::Unsupported;
    }
    return DirectYuvSupport::Unsupported;
}

/// Whether a query resolves to the certified direct-YUV set.
[[nodiscard]] inline bool is_certified_direct_yuv(
    const LayerYuvQuery& query) noexcept {
    return classify_direct_yuv(query) == DirectYuvSupport::Certified;
}

/// DirectYuv eligibility policy for a composition/job.  `Auto` allows an
/// explicit FullGraph fallback; `Require` hard-fails (never approximates).
enum class DirectYuvPolicy : std::uint8_t {
    Auto = 0,
    Require = 1,
};

/// Resolved eligibility of one layer for the DirectYUV lane.
struct DirectYuvEligibility {
    bool eligible{false};
    DirectYuvSupport support{DirectYuvSupport::Unsupported};
    /// True when this layer alone makes the DirectYUV lane invalid.
    bool blocking{false};
    /// True when the caller must abort instead of falling back (Require).
    bool hard_fail{false};
    /// Short, stable reason class (no per-layer name; callers append it).
    std::string_view reason{"unsupported"};
};

/// Resolve DirectYUV eligibility for one layer feature query under the given
/// job policy.  `Auto` + non-certified → eligible=false, blocking=true but
/// hard_fail=false (caller falls back to FullGraph explicitly); `Require` +
/// non-certified → hard_fail=true.
[[nodiscard]] inline DirectYuvEligibility resolve_direct_yuv_support(
    const LayerYuvQuery& query, DirectYuvPolicy policy) noexcept {
    const DirectYuvSupport support = classify_direct_yuv(query);
    DirectYuvEligibility result;
    result.support = support;
    if (support == DirectYuvSupport::Certified) {
        result.eligible = true;
        result.blocking = false;
        result.hard_fail = false;
        result.reason = "certified";
        return result;
    }
    result.eligible = false;
    result.blocking = true;
    result.hard_fail = policy == DirectYuvPolicy::Require;
    switch (support) {
        case DirectYuvSupport::RequiresFullFrame:
            result.reason = "requires_full_frame_rgba";
            break;
        case DirectYuvSupport::CpuOnly:
            result.reason = "cpu_bound_effect";
            break;
        case DirectYuvSupport::Certified:
            break;
        case DirectYuvSupport::Unsupported:
            if (!query.two_d_transform) result.reason = "three_d_transform";
            else if (query.mask_enabled) result.reason = "mask_enabled";
            else if (query.projection_enabled) result.reason = "projection_enabled";
            else if (!query.single_shape_node) result.reason = "multi_node_layer";
            else if (query.effect == DirectYuvEffectClass::TextOverlay &&
                     !query.text_style_representable)
                result.reason = "text_style_unsupported";
            else result.reason = "unsupported_blend_mode";
            break;
    }
    return result;
}

/// Stable string-form name for a DirectYuvSupport value. Logging only.
[[nodiscard]] inline std::string_view direct_yuv_support_name(
    DirectYuvSupport support) noexcept {
    switch (support) {
        case DirectYuvSupport::Certified:         return "certified";
        case DirectYuvSupport::RequiresFullFrame: return "requires_full_frame";
        case DirectYuvSupport::CpuOnly:           return "cpu_only";
        case DirectYuvSupport::Unsupported:       return "unsupported";
    }
    return "unknown";
}

} // namespace chronon3d::media::video
