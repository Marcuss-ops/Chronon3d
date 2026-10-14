#pragma once

// Canonical tile-prune policy. FrameDeltaCompiler calculates dirty bounds and
// the DirtyTileMask; ExecutionResolver alone decides whether those tiles are
// executable. Coordinators and node helpers only consume that decision.

#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include "scene_internal.hpp"
#include "../core/scene_fingerprint.hpp"
#include <chronon3d/render_graph/pipeline/execution_decision.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d { class SoftwareRenderer; class Framebuffer; }
namespace chronon3d::effects { class EffectCatalog; }

namespace chronon3d::graph {

struct TileDecision {
    bool enabled{false};
    FrameExecutionPath path{FrameExecutionPath::FullRgb};
    bool decode{false};
    bool composite{true};
    bool encode{false};
    std::string_view reason_if_disabled;
};

struct FrameExecutionPlan {
    FrameExecutionPath path{FrameExecutionPath::FullRgb};
    bool decode{false};
    bool render{true};
    bool composite{true};
    bool convert_to_rgb{false};
    bool convert_to_yuv{false};
    bool encode{false};
    std::optional<raster::BBox> dirty_region;
    std::optional<raster::DirtyTileMask> dirty_tiles;
    std::vector<raster::BBox> dirty_regions;
    std::shared_ptr<runtime::RenderSurface> previous_surface;
    std::shared_ptr<Framebuffer> previous_framebuffer;
    bool copy_previous_surface{false};
    std::optional<CopyGopPlan> copy_gop_plan;
    std::string_view reason;
    bool use_dirty_region{false};
    bool force_full_frame_clear{false};
    std::shared_ptr<runtime::RenderSurface> output_surface;
    std::shared_ptr<Framebuffer> reuse_surface;
    FrameFingerprints frame_fingerprints{};
    bool scene_structure_unchanged{false};
    bool static_camera_changed{true};
    bool scene_is_static{false};
};

class ExecutionResolver {
public:
    [[nodiscard]] static ExecutionDecision resolve_initial(const detail::FrameDelta&) noexcept;
    [[nodiscard]] static ExecutionDecision resolve_copy_gop(
        const detail::FrameDelta&, const CopyGopEligibility&) noexcept;
    static FrameExecutionPlan resolve_early_reuse(
        const RenderGraphContext&, const Scene&, Frame, int, int, SoftwareRenderer*);
    /// Native clean-frame reuse pre-pass (video exporter loop only).
    /// Side-effect-free: unlike resolve_early_reuse it records no telemetry,
    /// touches no surfaces and resolves no layers. Returns true when `frame`
    /// is pixel-identical to the previous completed native frame, so the
    /// exporter may skip the Vulkan render and re-submit the held encode
    /// surface with a new PTS. Any doubt returns false (full render).
    /// The scene camera is resolved internally with resolve_scene_camera,
    /// the same authority the render path uses. `registry_generation`
    /// mirrors the render path's fingerprint input (0 is valid and only
    /// narrows the static-scene sub-path, never the resolved-scene path);
    /// null frame parameters mean "no typed payload". `scene` must be the
    /// frame's materialized scene (for static-topology compositions the
    /// stable template scene).
    [[nodiscard]] static bool native_clean_reuse_eligible(
        const Scene&, Frame, std::uint64_t registry_generation,
        const compiler::FrameParameterBlock*, const SoftwareRenderer*,
        bool has_previous_native_surface, std::string& reason_out);
    [[nodiscard]] static std::vector<raster::BBox> coalesce_dirty_regions(
        const raster::TileGrid&, const raster::DirtyTileMask&);
    static FrameExecutionPlan resolve(
        FrameExecutionPlan, const detail::LayerResolutionResult&, const Scene&,
        const Camera2_5D&, const RenderSettings&, const detail::DirtyRectOutput&,
        double, SoftwareRenderer*, Frame, int, int,
        const effects::EffectCatalog* = nullptr, bool = false, bool = false,
        runtime::PixelFormat = runtime::PixelFormat::Rgba32Float,
        const std::optional<CopyGopEligibility>& = std::nullopt);
    static TileDecision decide(
        const detail::LayerResolutionResult&, const RenderSettings&,
        const detail::DirtyRectOutput&, double, const SoftwareRenderer*, Frame,
        const effects::EffectCatalog* = nullptr);
};
} // namespace chronon3d::graph
