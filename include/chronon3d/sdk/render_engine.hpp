#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/sdk/render_engine.hpp
//
// P1-A — Skeleton SDK entry point for `sdk::RenderEngine`.
// ═════════════════════════════════════════════════════════════════════════════
//
// Neutral ABI-friendly façade.  PIMPL keeps this header from revealing
// any backend / runtime / cache / render-graph type in the public API.
//
// What lives here:
//   - constructor / destructor / move-only semantics
//   - primary render entry point: render(const Composition&, Frame)
//   - settings application: set_settings(const RenderSettings&)
//   - assets resolution: set_assets_root(std::filesystem::path)
//
// What does NOT live here:
//   - backend concrete types (SoftwareRenderer*, SoftwareRenderSession)
//   - runtime / session services (runtime::RenderRuntime&)
//   - cache concrete types (NodeCache, FrameCache, FramebufferPool)
//   - render-graph executor / graph-cache concrete types
//   - asset-registry handle
//
// This is the complete supported V0.1 engine surface. Internal adapters,
// compatibility accessors, and concrete graph services are not exported.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/types/result.hpp>     // canonical Result<T, E> (only allowed non-sdk include)
#include <chronon3d/sdk/render_error.hpp>      // sdk::RenderError / RenderErrorCode
#include <chronon3d/sdk/render_file_request.hpp>
#include <chronon3d/sdk/render_output.hpp>     // sdk::RenderOutput / PixelFormat
#include <chronon3d/sdk/render_request.hpp>    // sdk::Frame / RenderRequest
#include <chronon3d/sdk/render_settings.hpp>   // sdk::RenderSettings

#include <filesystem>
#include <functional>
#include <memory>
#include <cstdint>
#include <string_view>

namespace chronon3d {
// Forward-declare the canonical `Composition` type
// (`<chronon3d/timeline/composition.hpp>`).  Forward-declared here so
// this header never pulls the camera_v1 / asset_registry / asset
// transitives from the timeline umbrella.
class Composition;
struct CompiledComposition;
}

namespace chronon3d::sdk {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warning, Error };

using LogCallback = std::function<void(
    LogLevel level, std::string_view component, std::string_view message,
    void* user)>;

/// Neutral SDK render facade.
class RenderEngine {
public:
    // ── Construction / destruction / move ─────────────────────────
    RenderEngine();
    explicit RenderEngine(RenderSettings settings);

    /// Out-of-line destructor: required because `std::unique_ptr<Impl>`
    /// needs the complete `Impl` type at the point of destruction;
    /// the definition lives in src/ where `Impl` is fully defined.
    ~RenderEngine();

    // Rule 4 (ANTI_DUPLICATION_RULES): move-only, never copy.
    RenderEngine(const RenderEngine&)            = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    RenderEngine(RenderEngine&&) noexcept;
    RenderEngine& operator=(RenderEngine&&) noexcept;

    // ── Primary API ───────────────────────────────────────────────

    /// Render one frame from `composition` at `frame`.  Returns the
    /// produced `RenderOutput` (owned OPP-side buffer) or a structured
    /// `RenderError`.
    [[nodiscard]]
    chronon3d::Result<RenderOutput, RenderError>
    render(const chronon3d::Composition& composition, Frame frame);

    /// Render an immutable precompiled composition without recompiling its
    /// scene/camera program at the frame boundary.  This is the execution
    /// entry for PreparedRenderPlan consumers; the public header keeps the
    /// compiled type opaque.
    [[nodiscard]]
    chronon3d::Result<RenderOutput, RenderError>
    render_compiled(const chronon3d::CompiledComposition& compiled,
                    Frame frame);

    /// Render a contiguous frame range into an encoded video file.
    /// This is a public wrapper over the same internal frame renderer and
    /// VideoSink used by the canonical CLI pipeline.
    [[nodiscard]]
    chronon3d::Result<RenderReport, RenderError>
    render_to_file(const RenderFileRequest& request,
                   const RenderCallbacks& callbacks = {});

    /// Apply new render settings.  Replaces whatever was set in the
    /// constructor or by a prior `set_settings` call.  Thread-safe
    /// relative to `render()` (the engine snapshots settings at the
    /// top of `render()`).
    void set_settings(const RenderSettings& settings);

    /// Set the assets root directory used to resolve fonts, images,
    /// and other relocatable resources.
    void set_assets_root(std::filesystem::path root);

    /// Install an optional per-engine diagnostic sink.
    void set_log_callback(LogCallback callback, void* user = nullptr);

private:
    /// PImpl; the OPP-side definition lives in src/ and is the only
    /// place that can reference backends, runtime, caches, and graph.
    struct Impl;

    /// PIMPL gate: this unique_ptr is the entire reason this header
    /// has zero compile-time visibility into the concrete engine.
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d::sdk
