#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/core/effect_stack.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>
#include <chronon3d/compositor/blend_mode.hpp>
#include <chronon3d/compositor/composite_operator.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/effects/effect_execution_context.hpp>
#include <glm/glm.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>

namespace chronon3d {
    struct RenderNode;
    struct RenderState;
    struct RenderCounters;
    struct TextRunShape;  // forward decl for draw_text_run
}

namespace chronon3d::cache {
    class FramebufferPool;
}

namespace chronon3d::graph {

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
};

/// Minimal Result type for backend operations.
template <typename T, typename E>
class Result {
public:
    Result(T value) : m_storage(std::move(value)) {}
    Result(E error) : m_storage(std::move(error)) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(m_storage); }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const { return std::get<T>(m_storage); }
    [[nodiscard]] const E& error() const { return std::get<E>(m_storage); }

private:
    std::variant<T, E> m_storage;
};

using RenderOpResult = Result<RenderOpOutcome, RenderBackendError>;

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

    /// Per-pixel depth-of-field blur.  Backends must implement.
    virtual void apply_per_pixel_dof(
        Framebuffer& framebuffer,
        std::span<const float> depth,
        const DepthOfFieldSettings& dof,
        const LensModel& lens,
        const std::optional<raster::BBox>& clip) = 0;

    virtual void draw_node(
        Framebuffer& /*fb*/,
        const RenderNode& /*node*/,
        const RenderState& /*state*/,
        const Camera& /*camera*/,
        int /*width*/,
        int /*height*/
    ) {
        // Default no-op: draw_node is implemented on SoftwareRenderer
        // (which inherits RenderBackend directly), not on SoftwareBackend.
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
};

} // namespace chronon3d::graph
