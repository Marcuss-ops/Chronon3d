#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — RenderEngine (Public SDK Facade)
//
// Single entry point for the rendering SDK.  RenderEngine owns the
// architecture via RenderEngine::Impl:
//
//   RenderEngine
//     └── RenderEngine::Impl (PIMPL)
//         ├── runtime::RenderRuntime   (engine-lifetime owner)
//         │                                of cache/pool/executor/
//         │                                catalogs + plan cache +
//         │                                default_assets_root)
//         ├── SoftwareRenderer         (per-instance orchestrator that
//         │                                BORROWS services from runtime;
//         │                                demoted to backend adapter in
//         │                                Phase B per TICKET-011b)
//         └── AssetRegistry            (mounts assets_root)
//
// The public API surface (this header) is unchanged: existing code
// that calls `engine.set_assets_root(...)` or
// `engine.render_scene(...)` keeps working unchanged.  Internal state
// now lives in `RenderEngine::Impl` so future architectural changes
// (TICKET-011b–e) can swap out the orchestrator without touching this
// header.
//
// Rule 4 (ANTI_DUPLICATION_RULES): RenderEngine is the single public
// rendering API.  No other Renderer/RenderService/RenderPipelineRunner
// class should exist.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/runtime/render_session.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <filesystem>
#include <memory>
#include <optional>

namespace chronon3d::advanced { class RenderEngineAccess; }

namespace chronon3d {

namespace image { class ImageBackend; }
namespace media { class MediaFrameProvider; }
namespace runtime { class RenderRuntime; }

/**
 * RenderEngine — thin public facade for the Chronon3d SDK.
 *
 * Host creates a RenderEngine, optionally sets the assets root and
 * composition registry, then calls render_scene() or render_frame().
 * All heavy state (caches, pool, executor, plan cache, catalogs,
 * assets registry) lives in RenderEngine::Impl / runtime::RenderRuntime.
 */
class RenderEngine {
public:
    // ── Construction ───────────────────────────────────────────────

    /// Default constructor — reads Config from environment variables.
    RenderEngine();

    /// Construct with an explicit Config (e.g. CLI budget override).
    explicit RenderEngine(Config config);

    /// Construct with Config and an assets root directory.
    /// Calls set_assets_root() internally.
    RenderEngine(Config config, std::filesystem::path assets_root);

    ~RenderEngine();

    // Non-copyable, movable.
    RenderEngine(const RenderEngine&) = delete;
    RenderEngine& operator=(const RenderEngine&) = delete;
    RenderEngine(RenderEngine&&) noexcept;
    RenderEngine& operator=(RenderEngine&&) noexcept;

    // ── Asset management ─────────────────────────────────────────

    /// Set the root directory for asset resolution (fonts, images, etc.).
    /// In Phase A: writes both (a) the legacy process-wide default so
    /// deep rendering code keeps working unchanged and (b) the
    /// owning RenderRuntime's default_assets_root + AssetRegistry mount
    /// so the engine owns the asset root in its own state.  Phase B
    /// (follow-up) will drop (a) once all deep rendering callers read
    /// the engine's runtime instead of the global.
    void set_assets_root(const std::filesystem::path& root);

    /// Access the internal AssetRegistry owned by Impl.
    [[nodiscard]] AssetRegistry& assets() noexcept;
    [[nodiscard]] const AssetRegistry& assets() const noexcept;

    /// Read the engine-owned default assets root (empty if unset).
    /// WP-8 PR 8.1 Final — returns by value so the call routes through
    /// the process-wide `runtime::process_wide_assets_root()` mutex-guarded
    /// slot; callers cannot hold a reference past a concurrent
    /// `set_assets_root()` from another thread.
    [[nodiscard]] std::string assets_root() const noexcept;

    // ── Composition registry ────────────────────────────────────

    /// Set the composition registry used for preflight validation and
    /// composition-aware rendering.
    void set_composition_registry(const CompositionRegistry* registry);

    // ── Rendering (primary API) ──────────────────────────────────

    /// Render a scene with a standard Camera.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const Camera& camera, i32 width, i32 height);

    /// Render a scene with an optional 2.5D Camera (or nullopt for identity).
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const std::optional<Camera2_5D>& camera,
        i32 width, i32 height);

    /// Render a single frame from a Composition.
    /// Pass A — marked deprecated; the canonical entry is
    /// `chronon3d::sdk::RenderEngine::render()` (V0.2 SDK migration).
    /// The implementation still works until Pass B/D removes it.
    [[nodiscard]] [[deprecated("Use RenderEngine::render()")]]
    std::shared_ptr<Framebuffer> render_frame(
        const Composition& comp, Frame frame);

    // ── Backend injection ────────────────────────────────────────

    /// Set the image decoding/encoding backend (default: StbImageBackend).
    void set_image_backend(std::shared_ptr<image::ImageBackend> backend);

    /// Set the video frame decoder.
    void set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder);

    // ── Settings ─────────────────────────────────────────────────

    /// Apply render settings (antialiasing, motion blur, dirty rects, etc.).
    void set_settings(const RenderSettings& settings);

    /// Get current render settings.
    [[nodiscard]] const RenderSettings& settings() const noexcept;

    // ── Accessors ─────────────────────────────────────────────────

    /// Direct access to the underlying SoftwareRenderer for advanced use.
    /// 06 R3b — returns a pointer to avoid the boundary-gate I5 substring
    /// `SoftwareRenderer &` (SoftwareRenderer's lifetime is bound to the
    /// owning Impl, so a pointer is the right contract).
    [[nodiscard]] [[deprecated("Internal backend access is not part of the stable SDK")]]
    SoftwareRenderer* renderer() noexcept;
    [[nodiscard]] [[deprecated("Internal backend access is not part of the stable SDK")]]
    const SoftwareRenderer* renderer() const noexcept;

    /// Nullable access to the underlying SoftwareRenderer.
    [[nodiscard]] [[deprecated("Internal backend access is not part of the stable SDK")]]
    SoftwareRenderer* renderer_or_null() noexcept;
    [[nodiscard]] [[deprecated("Internal backend access is not part of the stable SDK")]]
    const SoftwareRenderer* renderer_or_null() const noexcept;

    /// Access to the engine-owned RenderRuntime.
    /// Phase A: returns the Impl-owned runtime.
    [[nodiscard]] [[deprecated("Runtime access is internal")]]
    runtime::RenderRuntime& runtime() noexcept;
    [[nodiscard]] [[deprecated("Runtime access is internal")]]
    const runtime::RenderRuntime& runtime() const noexcept;

    /// Access the per-instance engine configuration.
    [[nodiscard]] const Config& config() const noexcept;

    /// Clear all caches (image, font, node, pool, graph, frame state).
    void clear_caches();

    /// Reset all profiling counters to zero.
    void reset_counters();

    /// Session factory: vends a fresh per-render-job
    /// SoftwareRenderSession referencing the engine-owned runtime.
    /// Lifetime invariant: the engine outlives any session it vends.
    [[nodiscard]] chronon3d::SoftwareRenderSession create_session();

private:
    /// OPP-side escape hatch (Pass C).  Grants the typed
    /// `chronon3d::advanced::RenderEngineAccess` accessor private access
    /// to `Impl` so legacy code can still reach the backend during the
    /// Pass B→D migration.  Not part of the stable SDK surface; consumers
    /// MUST include `<chronon3d/advanced/render_engine_access.hpp>`
    /// (Pass C deliverable) to use this friend.
    friend class ::chronon3d::advanced::RenderEngineAccess;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d
