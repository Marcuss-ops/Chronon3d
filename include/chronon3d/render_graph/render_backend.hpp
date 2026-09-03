#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <typeindex>
#include <utility>
#include <variant>
#include <vector>

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
// RenderBackend capabilities & error types
// ═══════════════════════════════════════════════════════════════════════════

/// Backend feature flags queried by the graph compiler at planning time.
struct RenderCapabilities {
    bool text_run{false};  // supports per-glyph batched text-run rendering
};

/// Discrete error codes returned by RenderBackend operations.
enum class RenderBackendErrorCode {
    UnsupportedCapability,  // backend does not support the requested operation
    InvalidInput,           // caller passed malformed / empty data
    ExecutionFailure,       // operation failed at runtime (shaping, raster, etc.)
};

/// PR2 — stable, human-readable name for each error code.  Used by log
/// messages (`spdlog::error("[backend] draw_text_run failed: [{}] {}",
/// render_backend_error_code_name(code), message)`).  Centralising this
/// mapping prevents log strings from drifting across callers.
inline const char* render_backend_error_code_name(RenderBackendErrorCode code) noexcept {
    switch (code) {
        case RenderBackendErrorCode::UnsupportedCapability: return "UnsupportedCapability";
        case RenderBackendErrorCode::InvalidInput:         return "InvalidInput";
        case RenderBackendErrorCode::ExecutionFailure:     return "ExecutionFailure";
    }
    return "Unknown";  // unreachable in well-formed enum usage
}

struct RenderBackendError {
    RenderBackendErrorCode code{RenderBackendErrorCode::ExecutionFailure};
    std::string message{};
};

struct RenderOpOutcome {
    /// Number of items successfully processed (e.g. glyphs rasterized,
    /// shapes drawn).  Zero is a valid outcome when there is nothing to do
    /// (e.g. layout is empty, safe-bbox clip rejects the layer).
    std::size_t items_drawn{0};

    /// Intrinsic ink bounding box produced by the rasterizer (currently
    /// populated by the text-run processor).  When present, node_runner
    /// uses this instead of scanning the entire framebuffer to reconcile
    /// the predicted bbox.
    std::optional<raster::BBox> actual_ink_bbox{};
};

/// Minimal Result type for backend operations.
template <typename T, typename E>
class Result {
public:
    Result(T value) : m_storage(std::move(value)) {}
    Result(E error) : m_storage(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_storage); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
    /// M1.5#7 — match chronon3d::Result<T,E>::has_value() and
    /// std::expected::has_value() API surface.
    [[nodiscard]] bool has_value() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const { return std::get<T>(m_storage); }
    [[nodiscard]] T& value() { return std::get<T>(m_storage); }
    [[nodiscard]] T take_value() { return std::move(std::get<T>(m_storage)); }  // P0-1: move-only types
    [[nodiscard]] const E& error() const { return std::get<E>(m_storage); }

private:
    std::variant<T, E> m_storage;
};

using RenderOpResult = Result<RenderOpOutcome, RenderBackendError>;

/// P0-1 — frame-level error surfaced by a render-graph node when its backend
/// dispatch fails.  Carried on RenderGraphContext via a shared mutable slot
/// so the executor can propagate the failure to the frame level (return
/// nullptr = documented "engine error / fall back to empty fb").
///
/// P0-1 closes the false-success pattern where TextRunNode::execute()
/// returned a valid framebuffer after a backend draw_text_run() failure.
struct NodeExecutionError {
    RenderBackendErrorCode backend_code{RenderBackendErrorCode::ExecutionFailure};
    std::string node_name{};   // which node reported the failure
    std::string message{};     // free-form diagnostic (source: RenderBackendError::message)
};

/// Result type for render-graph node execution.
/// On success: carries an OwnedFB (pool-owned framebuffer).
/// On failure: carries a NodeExecutionError that the executor propagates
/// to frame-level failure (GraphExecutor returns nullptr).
using NodeExecResult = Result<OwnedFB, NodeExecutionError>;

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

    /// Query backend capabilities at planning time.
    [[nodiscard]] virtual RenderCapabilities capabilities() const noexcept {
        return RenderCapabilities{};
    }

    virtual RenderCounters* counters() { return nullptr; }
    virtual std::shared_ptr<cache::FramebufferPool> framebuffer_pool() { return nullptr; }

    /// Export backend-specific GPU counters for the telemetry
    /// render_counters table (e.g. the vkQueueSubmit count and the number of
    /// executed command-plan passes).  The default is a no-op so software
    /// backends contribute nothing; GPU backends override this to feed their
    /// counters into the run record.  Counters are name/value pairs so the
    /// render_graph layer never depends on the telemetry record types.
    virtual void export_gpu_telemetry_counters(
        std::vector<std::pair<std::string, std::uint64_t>>& /*out*/) const {}

    /// Video exporters may retain the final device surface and consume it on
    /// the writer thread.  The default keeps the historical CPU readback
    /// contract; native GPU backends override this only when the surface can
    /// be handed off without exposing backend types to the graph.
    [[nodiscard]] virtual bool supports_native_video_surface() const noexcept {
        return false;
    }

    /// Query whether this backend supports native GPU surface handles.
    [[nodiscard]] virtual bool supports_native_surfaces() const noexcept {
        return false;
    }

    /// Query whether this backend supports GPU fused batch execution.
    [[nodiscard]] virtual bool is_batching_supported() const noexcept {
        return supports_native_surfaces();
    }

    virtual RenderOpResult create_video_encode_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::create_video_encode_surface: unsupported"});
    }

    virtual RenderOpResult copy_surface_to_video_encode(
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*destination*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::copy_surface_to_video_encode: unsupported"});
    }


    /// Frame-batching lifecycle.  Backends that record many passes into a
    /// single submission override these; the default is a no-op so
    /// single-pass/software backends are unaffected.  The frame orchestrator
    /// calls begin_frame_batch() before graph execution and end_frame_batch()
    /// once all passes are recorded, so a batching backend performs exactly
    /// one submit per frame instead of one operation-per-submit.
    virtual void begin_frame_batch() {}
    virtual void end_frame_batch() {}

    /// Plan-driven variant of begin_frame_batch().  The command-plan
    /// executor calls it with the frame's CommandPlan so a batching backend
    /// synchronizes every pass with the precise barriers from plan.barriers
    /// (instead of a conservative per-pass fallback) and consumes
    /// plan.resources to bind logical handles to physical slots (one backing
    /// VkImage per slot, so lifetime-disjoint surfaces alias).  The default
    /// is a no-op so single-pass/software backends are unaffected.
    virtual void begin_plan_batch(const runtime::CommandPlan& /*plan*/) {}

    /// Bind an external dynamic parameter span for the next compiled pass.
    /// The default is intentionally a no-op so legacy/software backends keep
    /// their node.execute() fallback. Vulkan implementations may upload or
    /// bind the span without changing the domain-neutral graph contract.
    virtual void bind_compiled_parameters(std::span<const std::byte> /*parameters*/) {}

    /// Command-batch lifecycle.  While a command batch is active, a batching
    /// backend defers the single submission it would otherwise perform per
    /// frame batch and instead records N overlays/frames into one command
    /// batch, submitting them all with exactly one queue submission at
    /// end_command_batch().  This is the "N overlays in one command batch"
    /// primitive: the caller wraps N begin_frame_batch()/end_frame_batch()
    /// (or N execute_command_plan() calls) between begin_command_batch() and
    /// end_command_batch().  The default is a no-op so single-pass/software
    /// backends are unaffected (each frame submits immediately as before).
    virtual void begin_command_batch() {}
    virtual void end_command_batch() {}

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
    virtual RenderOpResult draw_node(
        Framebuffer& /*fb*/,
        const RenderNode& /*node*/,
        const RenderState& /*state*/,
        const Camera& /*camera*/,
        int /*width*/,
        int /*height*/
    ) {
        return RenderOpResult(RenderOpOutcome{});
    }

    /// Validate a renderable shape before the graph reaches execution.
    /// Backends with a processor registry override this to resolve the
    /// processor once at the compiler/executor boundary.  The default is
    /// intentionally permissive for lightweight/mock backends.
    [[nodiscard]] virtual std::optional<RenderBackendError> validate_render_node(
        const RenderNode& /*node*/) const {
        return std::nullopt;
    }

    /// Legacy direct resolver retained for non-compiled backend callers.
    /// It returns a non-owning pointer valid only for the immediate call;
    /// callers must not retain it across registry/engine shutdown. Compiled
    /// graph construction uses processor_snapshot() instead and never calls
    /// this method during frame execution.
    [[deprecated("Use processor_snapshot() and handles instead")]]
    [[nodiscard]] virtual renderer::ShapeProcessor* resolve_shape_processor(
        const RenderNode& /*node*/) const noexcept {
        return nullptr;
    }

    /// Legacy direct resolver retained for non-compiled backend callers.
    /// It returns a non-owning pointer valid only for the immediate call;
    /// callers must not retain it across registry/engine shutdown. Compiled
    /// graph construction resolves effects through the immutable processor
    /// snapshot, with no per-frame registry lookup.
    [[deprecated("Use processor_snapshot() and handles instead")]]
    [[nodiscard]] virtual renderer::EffectProcessor* resolve_effect_processor(
        std::type_index /*params_type*/) const noexcept {
        return nullptr;
    }

    /// Immutable engine-local processor ownership captured at compile time.
    /// A backend that compiles renderable shape/effect nodes must return an
    /// owning snapshot; the compiled graph retains it for its full lifetime.
    [[nodiscard]] virtual std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept {
        return nullptr;
    }

    /// True for backends whose compiled shape/effect dispatch requires an
    /// owning processor snapshot. Lightweight test backends may leave this
    /// false and compile graph operators without processor bindings.
    [[nodiscard]] virtual bool requires_processor_snapshot() const noexcept {
        return false;
    }

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
        const runtime::SurfaceDesc& /*desc*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::create_surface: native surfaces are not supported"});
    }

    virtual RenderOpResult release_surface(
        runtime::RenderSurfaceHandle /*handle*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::release_surface: native surfaces are not supported"});
    }

    /// Returns whether a logical native surface still has a live backend
    /// binding. This is distinct from RenderSurfaceRegistry::lookup(): a
    /// cached framebuffer can outlive the physical GPU binding it referenced.
    [[nodiscard]] virtual bool is_native_surface_valid(
        runtime::RenderSurfaceHandle /*handle*/) const noexcept { return false; }

    /// Reclaim all backend-owned frame-transient surfaces at a job boundary.
    /// JobPersistent asset/font surfaces are intentionally retained.
    virtual void release_frame_transient_surfaces() noexcept {}

    /// Retire frame-transient GPU resources without draining the whole device.
    /// Implementations may defer destruction until the owning submission fence
    /// has completed.  This is the per-frame path; the method above remains
    /// the final, blocking job cleanup path.
    virtual void retire_frame_transient_surfaces() noexcept {}

    virtual RenderOpResult upload_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface: native surfaces are not supported"});
    }

    /// Upload a tightly packed RGBA rectangle into an existing surface.
    /// Pixels outside the rectangle remain unchanged.
    virtual RenderOpResult upload_surface_region(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::int32_t /*x*/, std::int32_t /*y*/,
        std::uint32_t /*width*/, std::uint32_t /*height*/,
        std::span<const float> /*rgba*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface_region: native surfaces are not supported"});
    }

    virtual RenderOpResult upload_surface_async(
        runtime::RenderSurfaceHandle /*handle*/,
        const runtime::SurfaceDesc& /*desc*/,
        std::span<const float> /*rgba*/,
        runtime::UploadTicket& /*ticket*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::upload_surface_async: native surfaces are not supported"});
    }

    virtual RenderOpResult wait_upload(const runtime::UploadTicket& /*ticket*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::wait_upload: native surfaces are not supported"});
    }

    virtual RenderOpResult download_surface(
        runtime::RenderSurfaceHandle /*handle*/,
        std::span<float> /*rgba*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::download_surface: native surfaces are not supported"});
    }

    virtual RenderOpResult composite_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        BlendMode /*mode*/ = BlendMode::Normal,
        CompositeOperator /*op*/ = CompositeOperator::SourceOver,
        const std::optional<raster::BBox>& /*clip*/ = std::nullopt) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::composite_surfaces: native surfaces are not supported"});
    }

    /// Replace-copy a native surface, optionally limited to a canvas clip.
    /// Unlike SourceOver this preserves exact source pixels and is used by
    /// native effects to merge a clipped result without a CPU round trip.
    virtual RenderOpResult copy_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const std::optional<raster::BBox>& /*clip*/ = std::nullopt) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::copy_surface: native surfaces are not supported"});
    }

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
        const Color& /*color*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::fill_rect_surface: native surfaces are not supported"});
    }

    virtual RenderOpResult transform_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        int /*offset_x*/, int /*offset_y*/, float /*opacity*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::transform_surface: native surfaces are not supported"});
    }

    virtual RenderOpResult transform_surface_affine(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        const runtime::SurfaceAffineTransform& /*transform*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::transform_surface_affine: native surfaces are not supported"});
    }

    /// Apply one separable blur pass to native surfaces. Callers can issue
    /// horizontal then vertical passes without moving pixels through CPU.
    virtual RenderOpResult blur_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*radius*/, bool /*horizontal*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::blur_surface: native surfaces are not supported"});
    }

    /// Execute the canonical glow sequence using caller-planned scratch
    /// surfaces. Implementations should record the complete sequence as one
    /// backend submission when the native backend supports command batching.
    virtual RenderOpResult glow_surfaces(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        runtime::RenderSurfaceHandle /*scratch_horizontal*/,
        runtime::RenderSurfaceHandle /*scratch_vertical*/,
        float /*radius*/, float /*intensity*/, const Color& /*tint*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::glow_surfaces: native surfaces are not supported"});
    }

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
        runtime::YuvExecutionMode /*mode*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::yuv_overlay_surface: native YUV is not supported"});
    }

    virtual RenderOpResult color_adjust_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*source*/,
        float /*brightness*/, float /*contrast*/,
        const Color& /*tint*/, float /*tint_amount*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::color_adjust_surface: native surfaces are not supported"});
    }

    virtual RenderOpResult matte_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*target*/,
        runtime::RenderSurfaceHandle /*matte*/,
        bool /*luma*/, bool /*inverted*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::matte_surface: native surfaces are not supported"});
    }

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
    ) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_run: not supported by this backend (capabilities().text_run == false)"
        });
    }

    /// Draw a batched text run by sampling a packed glyph atlas.  Each
    /// GlyphInstance locates one glyph quad inside `atlas` and its placement
    /// in `destination`; the backend composites every instance in a single
    /// kernel dispatch (the GPU text-run primitive).  The CPU-rasterized
    /// glyph bitmaps must already be resident in `atlas`.  Backends without
    /// a text kernel return UnsupportedCapability.
    virtual RenderOpResult draw_text_run_surface(
        runtime::RenderSurfaceHandle /*destination*/,
        runtime::RenderSurfaceHandle /*atlas*/,
        std::span<const runtime::GlyphInstance> /*glyphs*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_run_surface: not supported by this backend"});
    }

    /// Timed text variant. Backends without GPU timed highlighting fall back
    /// to the regular batched text path, preserving visual compatibility.
    virtual RenderOpResult draw_text_run_surface_timed(
        runtime::RenderSurfaceHandle destination,
        runtime::RenderSurfaceHandle atlas,
        std::span<const runtime::GlyphInstance> glyphs,
        float current_frame,
        const Color& highlight_color,
        bool highlight_enabled) {
        (void)current_frame;
        (void)highlight_color;
        (void)highlight_enabled;
        return draw_text_run_surface(destination, atlas, glyphs);
    }

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
        std::span<const runtime::RenderSurfaceHandle> /*atlas_pages*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::draw_text_batch: not supported by this backend"});
    }

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
        std::span<const float> /*paints*/) {
        return RenderOpResult(RenderBackendError{
            RenderBackendErrorCode::UnsupportedCapability,
            "RenderBackend::execute_layer_batch: not supported by this backend"});
    }

    /// Canonical GpuLayerBatch entry point.  Keep the backend-facing IR in
    /// one place; the span overload above remains the ABI-compatible leaf
    /// used by concrete backends and legacy command-plan callers.
    virtual RenderOpResult execute_layer_batch(
        runtime::RenderSurfaceHandle destination,
        const runtime::GpuLayerBatch& batch,
        std::span<const runtime::RenderSurfaceHandle> resources,
        std::span<const float> transforms,
        std::span<const float> paints) {
        return execute_layer_batch(
            destination, std::span<const runtime::LayerInstance>(batch.instances),
            resources, transforms, paints);
    }

    /// Phase 5 — pre-allocate every physical GPU surface from the compiled
    /// interval-coloring plan.  Called once after prepare() and before the
    /// first frame.  GPU backends create all VkImages here; the default
    /// no-op is correct for software/reference backends.
    virtual void preallocate_plan_surfaces(
        std::uint32_t /*canvas_width*/,
        std::uint32_t /*canvas_height*/,
        const CompiledResourceTable& /*plan*/) {
        // Default: no-op (software/reference backends)
    }
};

} // namespace chronon3d::graph
