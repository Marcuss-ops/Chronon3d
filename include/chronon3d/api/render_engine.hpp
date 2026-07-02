#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — RenderEngine (OPP-INTERNAL legacy facade)
//
// OPP-internal rendering facade — NOT the canonical public API.
// External consumers MUST use chronon3d::sdk::RenderEngine instead.
//
// RenderEngine owns the architecture via RenderEngine::Impl:
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
// Rule 4 (ANTI_DUPLICATION_RULES): chronon3d::sdk::RenderEngine is the
// single public rendering API.  This class (chronon3d::RenderEngine) is
// the OPP-internal implementation; no other public RenderEngine class
// should exist.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/backends/software/render_settings.hpp>
#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <filesystem>
#include <memory>
#include <optional>

namespace chronon3d {

namespace image { class ImageBackend; }
namespace media { class MediaFrameProvider; }
namespace runtime { class RenderRuntime; }
// P1-E Pass E — `SoftwareRenderer` was relocated into
// `src/backends/software/include_private/`; this public SDK header
// only references it via opaque sidecar pointers + the OPP escape
// hatch in `advanced::RenderEngineAccess`, so a forward declaration is
// sufficient (no full include).
class SoftwareRenderer;

/**
 * RenderEngine — OPP-INTERNAL rendering facade.
 *
 * @deprecated This class is the OPP-internal (legacy) RenderEngine.
 *   It is NOT part of the public SDK umbrella and is excluded from
 *   the canonical V0.1 public API surface.  External consumers MUST
 *   use `chronon3d::sdk::RenderEngine` instead (included via
 *   `<chronon3d/chronon3d.hpp>` or `<chronon3d/sdk/render_engine.hpp>`).
 *
 *   OPP-internal consumers (CLI, daemon, tests) that need the fuller
 *   API surface (set_image_backend, set_video_decoder, assets, config,
 *   clear_caches, reset_counters, settings) may continue to use this
 *   class but should include `<chronon3d/api/render_engine.hpp>`
 *   directly — the umbrella no longer re-exports it.
 *
 * Host creates a RenderEngine, optionally sets the assets root and
 * composition registry, then calls render_scene() or render().
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
        const Scene& scene, const Camera& camera, i32 width, i32 height, float fps);

    /// Render a scene with an optional 2.5D Camera (or nullopt for identity).
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const std::optional<Camera2_5D>& camera,
        i32 width, i32 height, float fps);

    /// Render a single frame from a Composition.
    /// Pass D — `render()` is now the canonical entry.  Replaces the
    /// `render_frame()` which was [[deprecated]] since Pass A and is
    /// now permanently removed.  The V0.2 SDK facade in
    /// `<chronon3d/sdk/render_engine.hpp>` (`sdk::RenderEngine::render`)
    /// returns a `Result<RenderOutput, …>` — that is the externally
    /// visible SDK surface; this `render()` is the OPP-internal entry
    /// used by `apps/chronon3d_cli`, tests, and content composition
    /// rendering, returning the raw `shared_ptr<Framebuffer>` for direct
    /// use by the OPP loop.  The OPP-side `SoftwareRenderer::render`
    /// carries the same name; the public facade here delegates through
    /// the OPP-side `RenderPipeline` instead of exposing the backend.
    [[nodiscard]]
    std::shared_ptr<Framebuffer> render(
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

    // ── Note (Pass D — V0.1 freeze) ────────────────────────────────────
    // The legacy accessors `renderer()`, `runtime()`, `create_session()`,
    // and their OPP-side `advanced::RenderEngineAccess` escape hatch have
    // all been REMOVED from the public V0.1 SDK surface.  The only entry
    // through the public facade is `RenderEngine::render(Composition, Frame)`
    // (above) which forwards into the OPP-side RenderPipeline.  Telemetry
    // requires linking the OPP-internal headers directly (none of the
    // counters surface is part of the V0.1 SDK — by design).

    /// Access the per-instance engine configuration.
    [[nodiscard]] const Config& config() const noexcept;

    /// Clear all caches (image, font, node, pool, graph, frame state).
    void clear_caches();

    /// Reset all profiling counters to zero.
    void reset_counters();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace chronon3d
