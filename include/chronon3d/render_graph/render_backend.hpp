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
#include <memory>
#include <optional>
#include <span>
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

/// Empty outcome payload signalling a successful backend operation.
struct RenderOpOutcome {};

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

using RenderOpResult = Result<RenderOpOutcome, RenderBackendErrorCode>;

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
        Framebuffer& fb,
        const RenderNode& node,
        const RenderState& state,
        const Camera& camera,
        int width,
        int height
    ) = 0;

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
    /// Returns RenderOpOutcome on success, or an error code on failure.
    /// Backends that do not support text-run rendering return
    /// RenderBackendErrorCode::UnsupportedCapability.
    virtual RenderOpResult draw_text_run(
        Framebuffer& /*fb*/,
        const chronon3d::TextRunShape& /*shape*/,
        const glm::mat4& /*model_matrix*/,
        float /*opacity*/,
        bool /*diagnostic_mode*/
    ) {
        return RenderBackendErrorCode::UnsupportedCapability;
    }
};

} // namespace chronon3d::graph
