#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — RenderEngine (Public SDK Facade)
//
// Single entry point for the rendering SDK.  Composes SoftwareRenderer,
// Config, and AssetRegistry into one object.  The CLI / host creates a
// RenderEngine, optionally sets the assets root and composition registry,
// then calls render_scene() or render_frame().
//
//   RenderEngine engine;
//   engine.set_assets_root("/path/to/assets");
//   engine.set_composition_registry(&registry);
//   auto fb = engine.render_scene(scene, camera, 1920, 1080);
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

/**
 * RenderEngine — thin public facade for the Chronon3d SDK.
 *
 * Owns the SoftwareRenderer, Config, and AssetRegistry.  Provides a
 * minimal, stable API for rendering scenes and frames.  This is the
 * type that external consumers (CLI, Python bindings, servers) should
 * create and interact with.
 *
 * Usage:
 *   RenderEngine engine;
 *   engine.set_assets_root("/assets");
 *   engine.set_composition_registry(&registry);
 *   auto fb = engine.render_scene(scene, camera, 1920, 1080);
 */
class RenderEngine {
public:
    // ── Construction ───────────────────────────────────────────────────

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
    RenderEngine(RenderEngine&&) noexcept = default;
    RenderEngine& operator=(RenderEngine&&) noexcept = default;

    // ── Asset management ───────────────────────────────────────────────

    /// Set the root directory for asset resolution (fonts, images, etc.).
    /// Mounts the path into the internal AssetRegistry and sets the
    /// default assets root for deep rendering code.
    ///
    /// The default assets root is set-once-per-process (not cleared on
    /// destruction).  Creating a new RenderEngine with a different root
    /// overwrites the previous value.
    void set_assets_root(const std::filesystem::path& root);

    /// Access the internal AssetRegistry.
    [[nodiscard]] AssetRegistry& assets() { return m_assets; }
    [[nodiscard]] const AssetRegistry& assets() const { return m_assets; }

    // ── Composition registry ───────────────────────────────────────────

    /// Set the composition registry used for preflight validation and
    /// composition-aware rendering.
    void set_composition_registry(const CompositionRegistry* registry) {
        m_renderer->set_composition_registry(registry);
    }

    // ── Rendering (primary API) ────────────────────────────────────────

    /// Render a scene with a standard Camera.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const Camera& camera, i32 width, i32 height);

    /// Render a scene with an optional 2.5D Camera (or nullopt for identity).
    [[nodiscard]] std::shared_ptr<Framebuffer> render_scene(
        const Scene& scene, const std::optional<Camera2_5D>& camera,
        i32 width, i32 height);

    /// Render a single frame from a Composition.
    [[nodiscard]] std::shared_ptr<Framebuffer> render_frame(
        const Composition& comp, Frame frame);

    // ── Backend injection ──────────────────────────────────────────────

    /// Set the image decoding/encoding backend (default: StbImageBackend).
    void set_image_backend(std::shared_ptr<image::ImageBackend> backend);

    /// Set the video frame decoder.
    void set_video_decoder(std::shared_ptr<media::MediaFrameProvider> decoder);

    // ── Settings ───────────────────────────────────────────────────────

    /// Apply render settings (antialiasing, motion blur, dirty rects, etc.).
    void set_settings(const RenderSettings& settings) {
        m_renderer->set_settings(settings);
    }

    /// Get current render settings.
    [[nodiscard]] const RenderSettings& settings() const {
        return m_renderer->settings();
    }

    // ── Accessors ──────────────────────────────────────────────────────

    /// Direct access to the underlying SoftwareRenderer for advanced use.
    [[nodiscard]] SoftwareRenderer& renderer() { return *m_renderer; }
    [[nodiscard]] const SoftwareRenderer& renderer() const { return *m_renderer; }

    /// Access the per-instance engine configuration.
    [[nodiscard]] const Config& config() const { return m_config; }

    /// Clear all caches (image, font, node, pool, graph, frame state).
    void clear_caches() { m_renderer->clear_caches(); }

    /// Reset all profiling counters to zero.
    void reset_counters() { m_renderer->reset_counters(); }

private:
    Config m_config;
    AssetRegistry m_assets;
    std::unique_ptr<SoftwareRenderer> m_renderer;
};

} // namespace chronon3d
