#pragma once

#include <chronon3d/core/error.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/memory/framebuffer_handle.hpp>
#include <chronon3d/math/raster_utils.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace chronon3d {
namespace renderer {
class ShapeProcessor;
class EffectProcessor;
class ProcessorRegistrySnapshot;
}
struct RenderNode;
struct RenderState;
struct RenderCounters;
struct TextRunShape;
}
namespace chronon3d::cache { class FramebufferPool; }

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
const char* render_backend_error_code_name(RenderBackendErrorCode code) noexcept;

struct RenderBackendError {
    RenderBackendErrorCode code{RenderBackendErrorCode::ExecutionFailure};
    std::string message{};

    /// Phase-1 bridge: preserve the legacy backend enum as detail while the
    /// executor consumes one control taxonomy.
[[nodiscard]] chronon::Error canonical_error(
        std::string component = "render_backend") const;
};

// Backend compatibility boundary. RenderBackendErrorCode remains the
// operation-level detail payload, while all construction is centralized so
// the executor receives one stable error shape.
[[nodiscard]] inline RenderBackendError make_render_backend_error(
    RenderBackendErrorCode code, std::string message) {
    return {code, std::move(message)};
}

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

    [[nodiscard]] chronon::Error canonical_error(
        std::int64_t frame = -1, std::uint64_t job_id = 0) const {
        const auto kind = backend_code == RenderBackendErrorCode::InvalidInput
            ? chronon::ErrorKind::InvalidPlan
            : backend_code == RenderBackendErrorCode::UnsupportedCapability
                ? chronon::ErrorKind::Recoverable
                : chronon::ErrorKind::FrameFailed;
        return chronon::from_legacy<RenderBackendErrorCode>(
            kind, "render_graph", "chronon3d::graph::RenderBackendErrorCode",
            render_backend_error_code_name(backend_code), message, "graph",
            node_name, {}, frame, job_id);
    }
};

/// Result type for render-graph node execution.
/// On success: carries an OwnedFB (pool-owned framebuffer).
/// On failure: carries a NodeExecutionError that the executor propagates
/// to frame-level failure (GraphExecutor returns nullptr).
using NodeExecResult = Result<OwnedFB, NodeExecutionError>;


} // namespace chronon3d::graph
