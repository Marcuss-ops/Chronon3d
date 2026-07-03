#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — RenderEngine (INTERNAL ADAPTER for sdk::RenderEngine)
//
// Fase A3 — This class is the OPP-internal adapter that powers the
// canonical public facade `chronon3d::sdk::RenderEngine` via PIMPL.
//
//   External consumer:
//     chronon3d::sdk::RenderEngine  (public API — returns Result<RenderOutput, …>)
//         │
//         └── PIMPL  ──wraps──▶  chronon3d::RenderEngine  (INTERNAL ADAPTER)
//                                     │
//                                     └── RenderEngine::Impl
//                                         ├── runtime::RenderRuntime
//                                         ├── SoftwareRenderer
//                                         └── AssetRegistry
//
// External consumers MUST use chronon3d::sdk::RenderEngine (included via
// <chronon3d/chronon3d.hpp> or <chronon3d/sdk/render_engine.hpp>).
//
// OPP-internal consumers (CLI daemon, tests, content composition) that
// need the fuller API surface (set_image_backend, set_video_decoder,
// assets, config, clear_caches, reset_counters) may continue to use
// this class as the internal adapter, but should include this header
// directly — the umbrella no longer re-exports it.
//
// Rules:
// - chronon3d::sdk::RenderEngine is the SINGLE public rendering API.
// - chronon3d::RenderEngine is the INTERNAL ADAPTER backing it.
// - No other public RenderEngine class should exist.
// - No new OPP-internal consumers should be created without ADR.
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
 * RenderEngine — INTERNAL ADAPTER for sdk::RenderEngine.
 *
 * Fase A3 — This is the OPP-internal adapter that backs the canonical
 * public SDK facade `chronon3d::sdk::RenderEngine` (PIMPL pattern).
 *
 * The `sdk::RenderEngine` PIMPL (in `src/runtime/sdk_render_engine.cpp`)
 * holds a `chronon3d::RenderEngine` instance and delegates all calls to
 * it.  The SDK facade returns `Result<RenderOutput, RenderError>`;
 * this adapter returns `shared_ptr<Framebuffer>` (float RGBA) for
 * OPP-internal consumers that need direct framebuffer access.
 *
 * @deprecated This class is the OPP-internal adapter.  It is NOT part
 *   of the public SDK umbrella and is excluded from the canonical V0.1
 *   public API surface.  External consumers MUST use
 *   `chronon3d::sdk::RenderEngine` instead (included via
 *   `<chronon3d/chronon3d.hpp>` or `<chronon3d/sdk/render_engine.hpp>`).
 *
 *   OPP-internal consumers (CLI daemon, tests) that need the fuller
 *   API surface may continue to use this class but must include
 *   `<chronon3d/api/render_engine.hpp>` directly.
 *
 *   No new OPP-internal consumers of this adapter should be created
 *   without an ADR justifying why the SDK facade is insufficient.
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
    /// P1 #7 — returns the engine-local resolver's mount root, NOT the
    /// process-wide global.  Two engines with different asset roots now
    /// observe their own value.  Returns by value; callers cannot hold a
    /// reference past a concurrent `set_assets_root()` from another thread.
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
