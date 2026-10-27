#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/core/error.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <chronon3d/render_graph/backend_selection.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/gpu_command_plan.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

#include <chronon3d/render_graph/render_backend_types.hpp>
#include <chronon3d/render_graph/render_backend_surface_types.hpp>

namespace chronon3d {
    namespace renderer {
        class ShapeProcessor;
        class EffectProcessor;
        class ProcessorRegistrySnapshot;
    }
    struct RenderNode;
    struct RenderState;
    struct RenderCounters;
    struct TextRunShape;  // forward decl for draw_text_run
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d::graph {

struct CompiledResourceTable;

// ═══════════════════════════════════════════════════════════════════════════
// RenderBackend
// ═══════════════════════════════════════════════════════════════════════════
class RenderBackend {
public:
    RenderBackend() = default;
    virtual ~RenderBackend() = default;
    RenderBackend(const RenderBackend&) = delete;
    RenderBackend& operator=(const RenderBackend&) = delete;
    RenderBackend(RenderBackend&&) noexcept = default;
    RenderBackend& operator=(RenderBackend&&) noexcept = default;

    /// Canonical resolved render lane for diagnostics: e.g. "software",
    /// "vulkan_native", "cuda_direct_yuv".
[[nodiscard]] virtual std::string lane_name() const;

    /// Query backend capabilities at planning time.
[[nodiscard]] virtual RenderCapabilities capabilities() const noexcept;
virtual RenderCounters* counters();
virtual std::shared_ptr<cache::FramebufferPool> framebuffer_pool();

    /// Export backend-specific GPU counters for the telemetry
    /// render_counters table (e.g. the vkQueueSubmit count and the number of
    /// executed command-plan passes).  The default is a no-op so software
    /// backends contribute nothing; GPU backends override this to feed their
    /// counters into the run record.  Counters are name/value pairs so the
    /// render_graph layer never depends on the telemetry record types.
virtual void export_gpu_telemetry_counters(
        std::vector<std::pair<std::string, std::uint64_t>>& /*out*/) const;

    /// Video exporters may retain the final device surface and consume it on
    /// the writer thread.  The default keeps the historical CPU readback
    /// contract; native GPU backends override this only when the surface can
    /// be handed off without exposing backend types to the graph.
[[nodiscard]] virtual bool supports_native_video_surface() const noexcept;

    /// Query whether this backend supports native GPU surface handles.
[[nodiscard]] virtual bool supports_native_surfaces() const noexcept;

    /// Query whether this backend supports GPU fused batch execution.
[[nodiscard]] virtual bool is_batching_supported() const noexcept;
virtual RenderOpResult create_video_encode_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/);
virtual RenderOpResult copy_surface_to_video_encode(
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*destination*/);


    /// Frame-batching lifecycle.  Backends that record many passes into a
    /// single submission override these; the default is a no-op so
    /// single-pass/software backends are unaffected.  The frame orchestrator
    /// calls begin_frame_batch() before graph execution and end_frame_batch()
    /// once all passes are recorded, so a batching backend performs exactly
    /// one submit per frame instead of one operation-per-submit.
virtual void begin_frame_batch();
virtual void end_frame_batch();

    /// Plan-driven variant of begin_frame_batch().  The command-plan
    /// executor calls it with the frame's CommandPlan so a batching backend
    /// synchronizes every pass with the precise barriers from plan.barriers
    /// (instead of a conservative per-pass fallback) and consumes
    /// plan.resources to resolve logical handles against physical slots.
    /// Lifetime-disjoint surfaces may alias; a backend can split
    /// content-carrying occupants when merging them would overwrite resident
    /// bytes. The default is a no-op so single-pass/software backends are
    /// unaffected.
virtual void begin_plan_batch(const runtime::CommandPlan& /*plan*/);

    /// Bind an external dynamic parameter span for the next compiled pass.
    /// The default is intentionally a no-op so legacy/software backends keep
    /// their node.execute() fallback. Vulkan implementations may upload or
    /// bind the span without changing the domain-neutral graph contract.
virtual void bind_compiled_parameters(std::span<const std::byte> /*parameters*/);

    /// Command-batch lifecycle.  While a command batch is active, a batching
    /// backend defers the single submission it would otherwise perform per
    /// frame batch and instead records N overlays/frames into one command
    /// batch, submitting them all with exactly one queue submission at
    /// end_command_batch().  This is the "N overlays in one command batch"
    /// primitive: the caller wraps N begin_frame_batch()/end_frame_batch()
    /// (or N plan executions) between begin_command_batch() and
    /// end_command_batch().  The default is a no-op so single-pass/software
    /// backends are unaffected (each frame submits immediately as before).
virtual void begin_command_batch();
virtual void end_command_batch();

    /// Per-pixel depth-of-field blur.  Backends must implement.
    virtual void apply_per_pixel_dof(
        Framebuffer& framebuffer,
        std::span<const float> depth,
        const DepthOfFieldSettings& dof,
        const LensModel& lens,
        const std::optional<raster::BBox>& clip) = 0;

    /// Draw a single RenderNode using the backend's rasteriser.
    ///
    /// Draw a single RenderNode and propagate backend failures to the graph.
    /// The default is NOT a silent success: a backend that cannot honour the
    /// request reports UnsupportedCapability so an unhandled node can never
    /// produce a transparent/incomplete frame without an error.  Backends
    /// that implement the node contract override this method.
virtual RenderOpResult draw_node(
        Framebuffer& /*fb*/,
        const RenderNode& /*node*/,
        const RenderState& /*state*/,
        const Camera& /*camera*/,
        int /*width*/,
        int /*height*/
    );

    /// Validate a renderable shape before the graph reaches execution.
    /// Backends with a processor registry override this to resolve the
    /// processor once at the compiler/executor boundary.  The default is
    /// intentionally permissive for lightweight/mock backends.
[[nodiscard]] virtual std::optional<RenderBackendError> validate_render_node(
        const RenderNode& /*node*/) const;



    /// Immutable engine-local processor ownership captured at compile time.
    /// A backend that compiles renderable shape/effect nodes must return an
    /// owning snapshot; the compiled graph retains it for its full lifetime.
[[nodiscard]] virtual std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept;

    /// True for backends whose compiled shape/effect dispatch requires an
    /// owning processor snapshot. Lightweight test backends may leave this
    /// false and compile graph operators without processor bindings.
[[nodiscard]] virtual bool requires_processor_snapshot() const noexcept;

    virtual void apply_effect_stack(
        Framebuffer& fb,
        const EffectStack& effects,
        const effects::EffectExecutionContext& context
    ) = 0;

    virtual void composite_layer(
        Framebuffer& dest,
        const Framebuffer& src,
        BlendMode mode,
        const std::optional<raster::BBox>& clip = std::nullopt,
        CompositeOperator op = CompositeOperator::SourceOver
    ) = 0;

    virtual void apply_blur(
        Framebuffer& fb,
        float radius,
        const std::optional<raster::BBox>& clip = std::nullopt
    ) = 0;

    /// Backend-neutral logical-surface contract. SoftwareBackend may keep
    /// using the legacy Framebuffer methods while GPU backends keep these
    /// surfaces device-local between passes.
virtual RenderOpResult create_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/);
virtual RenderOpResult release_surface(
        runtime::RenderSurfaceHandle /*handle*/);

    /// Returns whether a logical native surface still has a live backend
    /// binding. This is distinct from RenderSurfaceRegistry::lookup(): a
    /// cached framebuffer can outlive the physical GPU binding it referenced.
[[nodiscard]] virtual bool is_native_surface_valid(
        runtime::RenderSurfaceHandle /*handle*/) const noexcept;

    /// Reclaim all backend-owned frame-transient surfaces at a job boundary.
    /// JobPersistent asset/font surfaces are intentionally retained.
virtual void release_frame_transient_surfaces() noexcept;

    /// Retire frame-transient GPU resources without draining the whole device.
    /// Implementations may defer destruction until the owning submission fence
    /// has completed.  This is the per-frame path; the method above remains
    /// the final, blocking job cleanup path.
virtual void retire_frame_transient_surfaces() noexcept;
virtual RenderOpResult upload_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/);

    /// Upload a tightly packed RGBA rectangle into an existing surface.
    /// Pixels outside the rectangle remain unchanged.
virtual RenderOpResult upload_surface_region(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::int32_t /*x*/, std::int32_t /*y*/,
        std::uint32_t /*width*/, std::uint32_t /*height*/,
        std::span<const float> /*rgba*/);
virtual RenderOpResult upload_surface_async(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/,
        runtime::UploadTicket& /*ticket*/);

    struct UploadRows {
        std::span<const Color> first_row{};
        std::size_t stride{0};
    };
virtual RenderOpResult upload_surface_rows_async(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        const raster::BBox& /*clip*/,
        const UploadRows& /*rows*/,
        runtime::UploadTicket& /*ticket*/);
virtual RenderOpResult wait_upload(const runtime::UploadTicket& /*ticket*/);
virtual RenderOpResult download_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        std::span<float> /*rgba*/);
virtual RenderOpResult composite_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        BlendMode /*mode*/ = BlendMode::Normal,
        CompositeOperator /*op*/ = CompositeOperator::SourceOver,
        const std::optional<raster::BBox>& /*clip*/ = std::nullopt);

    /// Source-over composite a native surface after scaling all premultiplied
    /// RGBA channels by `opacity`. Used by deterministic temporal trails.
virtual RenderOpResult composite_surface_weighted(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*opacity*/,
        const std::optional<raster::BBox>& /*clip*/ = std::nullopt);

    /// Add a weighted premultiplied RGBA sample into a native accumulator.
    /// Unlike source-over compositing, every channel is accumulated as
    /// `destination += source * weight`; temporal shutter weights are
    /// normalized by the caller after the full sample plan is known.
virtual RenderOpResult accumulate_surface_weighted(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*weight*/);

    /// Replace-copy a native surface, optionally limited to a canvas clip.
    /// Unlike SourceOver this preserves exact source pixels and is used by
    /// native effects to merge a clipped result without a CPU round trip.
virtual RenderOpResult copy_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const std::optional<raster::BBox>& /*clip*/ = std::nullopt);

    /// Solid-color axis-aligned rectangle fill into a native surface.  The
    /// rectangle is half-open destination pixel space; pixels outside it are
    /// left untouched.  `color` must be PREMULTIPLIED RGBA (rgb already
    /// scaled by alpha), matching the surface storage convention the software
    /// compositor produces.  This is the first primitive of the GPU shape
    /// rasterizer (rect backgrounds / solid boxes).
virtual RenderOpResult fill_rect_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        std::int32_t /*x0*/, std::int32_t /*y0*/,
        std::int32_t /*x1*/, std::int32_t /*y1*/,
        const Color& /*color*/);

    /// Initialize a newly-created native surface to transparent without
    /// consuming a compiled render-plan pass slot.
virtual RenderOpResult initialize_transparent_surface(
        runtime::RenderSurfaceHandle /*destination*/);
virtual RenderOpResult transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        int /*offset_x*/, int /*offset_y*/, float /*opacity*/);
virtual RenderOpResult transform_surface_affine(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const runtime::SurfaceAffineTransform& /*transform*/);

    /// Apply one separable blur pass to native surfaces. Callers can issue
    /// horizontal then vertical passes without moving pixels through CPU.
virtual RenderOpResult blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*radius*/, bool /*horizontal*/, bool /*box_kernel*/ = false);

    /// Native per-pixel DOF depth stamp.  Covered pixels of `layer` (alpha >
    /// 0.01 inside the clip rect, origin at origin_x/origin_y) receive
    /// (depth, radius) in `depth`; `reset` initialises uncovered pixels to
    /// (-1, -1).  Mirrors the CPU publish_dof_provenance() rule.
virtual RenderOpResult dof_depth_stamp_surface(
        runtime::RenderSurfaceHandle /*depth*/,
        runtime::RenderSurfaceHandle /*layer*/,
        float /*depth_z*/, float /*radius*/,
        int /*clip_x0*/, int /*clip_y0*/, int /*clip_x1*/, int /*clip_y1*/,
        int /*origin_x*/, int /*origin_y*/, bool /*reset*/);

    /// One separable pass of the per-pixel DOF blur over the ROI, replicating
    /// the software kernel (highway_dof_kernels.cpp).  `depth` holds the
    /// (depth, radius) surface written by dof_depth_stamp_surface.
virtual RenderOpResult dof_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*depth*/,
        bool /*vertical*/,
        int /*roi_x0*/, int /*roi_y0*/, int /*roi_x1*/, int /*roi_y1*/,
        float /*max_radius*/, bool /*normalize_opaque*/);

    /// Uniform box-tap blur along a unit direction for native surfaces.
    /// destination[p] = average of tap_count bilinear samples spanning
    /// [-length/2, +length/2] along (dir_x, dir_y), matching the software
    /// DirectionalBlur processor (transparent out-of-bounds, raw premultiplied
    /// accumulation). Certified for length in (0, 64] and tap_count in [2, 64].
virtual RenderOpResult directional_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*dir_x*/, float /*dir_y*/, float /*length*/, int /*tap_count*/);

    /// Zoom+spin box-tap blur around a pixel center for native surfaces,
    /// matching the software RadialBlur processor (Clamp-edge bilinear, raw
    /// premultiplied accumulation, center-pixel identity). Certified for
    /// amount in (0, 8] and tap_count in [2, 64]; centroid-stable inputs.
virtual RenderOpResult radial_blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*center_x_px*/, float /*center_y_px*/,
        float /*amount*/, int /*tap_count*/);

    /// Bulk spatial-warp dispatch (warp_bulk.comp). The aux surface carries
    /// the displacement map for mode 12 and is ignored otherwise.
virtual RenderOpResult warp_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*aux*/,
        const WarpBulkParams& /*params*/);

    /// Bulk generator/stylize dispatch (generate_bulk.comp).
virtual RenderOpResult generate_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const GenerateBulkParams& /*params*/);

    /// Bulk LUT dispatch (lut_bulk.comp). The aux surface carries the baked
    /// Curves tables (mode 0) or the flattened Lut3D table (mode 1).
virtual RenderOpResult lut_bulk_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*aux*/,
        const LutBulkParams& /*params*/);

    /// Pointwise unsharp-mask combine for native surfaces: destination =
    /// source + amount * masked(source - blurred), alpha from source. The
    /// blurred input must come from the certified separable box-filter route.
virtual RenderOpResult sharpen_combine_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*blurred*/,
        float /*amount*/, float /*threshold*/);

    /// Execute the canonical glow sequence using caller-planned scratch
    /// surfaces. Implementations should record the complete sequence as one
    /// backend submission when the native backend supports command batching.
virtual RenderOpResult glow_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*scratch_horizontal*/,
        runtime::RenderSurfaceHandle /*scratch_vertical*/,
        float /*radius*/, float /*intensity*/, const Color& /*tint*/);

    /// Apply bright-pass thresholding, separable Gaussian blur, and additive
    /// bloom using the caller-planned scratch surfaces.
virtual RenderOpResult bloom_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*scratch_horizontal*/,
        runtime::RenderSurfaceHandle /*scratch_vertical*/,
        float /*threshold*/, float /*radius*/, float /*intensity*/);

    /// Apply brightness/contrast/tint to native surfaces without a CPU
    /// readback. The source and destination may be distinct transient slots.
    /// Composite a YUV-native overlay into an NV12/P010 destination. The
    /// backend must update luma and chroma planes consistently; sparse mode
    /// limits writes to the supplied dirty region.
virtual RenderOpResult yuv_overlay_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::PixelFormat /*format*/,
        const std::optional<raster::BBox>& /*dirty_region*/,
        runtime::YuvExecutionMode /*mode*/);
virtual RenderOpResult color_adjust_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*brightness*/, float /*contrast*/,
        const Color& /*tint*/, float /*tint_amount*/);

    /// Apply a native per-pixel color transform (see NativeColorTransform).
virtual RenderOpResult color_transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const NativeColorTransform& /*transform*/);

    /// Apply inverse-sampled, independent RGB channel transforms to a native
    /// RGBA surface. The output preserves the software processor's alpha and
    /// premultiplied-coverage semantics.
virtual RenderOpResult chromatic_transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const ChromaticAberrationParams& /*params*/);

    /// Apply a constant per-layer lighting multiplier to native RGBA surfaces.
    /// The layer's normal/light/material evaluation happens once on the CPU;
    /// the pixel multiply itself stays on the device and preserves alpha.
virtual RenderOpResult lighting_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const Color& /*multiplier*/);
virtual RenderOpResult matte_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*target*/,
        runtime::RenderSurfaceHandle /*matte*/,
        bool /*luma*/, bool /*inverted*/);

    /// Draw a batched text run with per-glyph animation state.
    /// Returns RenderOpOutcome on success, or a RenderBackendError on failure.
    /// Backends that do not support text-run rendering return
    /// RenderBackendErrorCode::UnsupportedCapability.
    /// PR2: the `diagnostic_mode` parameter was removed — diagnostic logging
    /// is now controlled by the caller (e.g. graph-node
    /// `ctx.policy.diagnostics_enabled`) and propagated into `spdlog::*`
    /// calls at the caller side, not as a flag buried inside the processor's
    /// params struct.
virtual RenderOpResult draw_text_run(
        Framebuffer& /*fb*/,
        const chronon3d::TextRunShape& /*shape*/,
        const glm::mat4& /*model_matrix*/,
        float /*opacity*/
    );

    /// Draw a batched text run by sampling a packed glyph atlas.  Each
    /// GlyphInstance locates one glyph quad inside `atlas` and its placement
    /// in `destination`; the backend composites every instance in a single
    /// kernel dispatch (the GPU text-run primitive).  The CPU-rasterized
    /// glyph bitmaps must already be resident in `atlas`.  Backends without
    /// a text kernel return UnsupportedCapability.
virtual RenderOpResult draw_text_run_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*atlas*/,
        std::span<const runtime::GlyphInstance> /*glyphs*/);

    /// Timed text variant. Backends without GPU timed highlighting fall back
    /// to the regular batched text path, preserving visual compatibility.
virtual RenderOpResult draw_text_run_surface_timed(
        runtime::RenderSurfaceHandle destination,
        runtime::RenderSurfaceHandle atlas,
        std::span<const runtime::GlyphInstance> glyphs,
        float current_frame,
        const Color& highlight_color,
        bool highlight_enabled);

    /// Draw a text run directly into the final destination surface — no
    /// intermediate text framebuffer, no clear, no separate composite.
    /// GlyphStatic[glyph_count] carries immutable atlas coordinates;
    /// TextRunDynamic carries transform/color shared by the whole run.
    /// `atlas_pages` maps atlas_page indices to the corresponding atlas
    /// texture handles (one per page).
virtual RenderOpResult draw_text_batch(
        runtime::RenderSurfaceHandle /*destination*/,
        std::span<const runtime::GlyphStatic> /*glyphs*/,
        std::span<const runtime::TextRunDynamic> /*runs*/,
        std::span<const runtime::RenderSurfaceHandle> /*atlas_pages*/);

    /// Execute a compiled GPU layer batch.  Every instance in `instances` is
    /// composited into `destination` in painter order using the backend's
    /// SSBO-based batch kernel.  `resources` maps resource_index to the
    /// texture/atlas handle to sample; `transforms` (optional) maps
    /// transform_index to a 4×4 matrix; `paints` (optional) maps paint_index
    /// to a colour uniform.  Backends without a batch kernel return
    /// UnsupportedCapability.
virtual RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle /*destination*/,
        std::span<const runtime::LayerInstance> /*instances*/,
        std::span<const runtime::RenderSurfaceHandle> /*resources*/,
        std::span<const float> /*transforms*/,
        std::span<const float> /*paints*/);

    /// Canonical GpuLayerBatch entry point.  Keep the backend-facing IR in
    /// one place; the span overload above remains the ABI-compatible leaf
    /// used by concrete backends and legacy command-plan callers.
virtual RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle destination,
        const runtime::GpuLayerBatch& batch,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms,
        std::span<const float> paints);

    /// DEMOLISHED (P1.4): RenderBackend::preallocate_plan_surfaces no longer
    /// exists.  No backend ever preallocated plan surfaces — the only
    /// override (Vulkan) explicitly disabled preallocation and pruned slots.
    /// Physical surface materialization is owned by each backend's surface
    /// authority, driven lazily by the CompiledResourceTable.
};

} // namespace chronon3d::graph
