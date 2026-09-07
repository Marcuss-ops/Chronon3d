#pragma once

#include <chronon3d/assets/asset_registry.hpp>
#include <chronon3d/assets/asset_resolver.hpp>
#include <chronon3d/assets/mesh_loader.hpp>
#include <chronon3d/backends/assets/image_cache.hpp>
#include <chronon3d/cache/cache_diagnostics.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>
#include <chronon3d/effects/curves.hpp>
#include <chronon3d/effects/effect_catalog.hpp>
#include <chronon3d/render_graph/cache/compiled_graph_cache.hpp>
#include <chronon3d/render_graph/executor/graph_executor.hpp>
#include <chronon3d/render_graph/pipeline/pipeline_catalogs.hpp>
#include <chronon3d/render_graph/registry/graph_node_catalog.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/runtime/gpu_asset_cache.hpp>
#include <chronon3d/runtime/gpu_glyph_atlas.hpp>
#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/media_session_pool.hpp>
#include <chronon3d/runtime/overlay_template.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_preparation.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace chronon3d {
class FontEngine;
}

namespace chronon3d::runtime {

/// Process/engine-lifetime owner for resources shared by jobs and frames.
///
/// JobSession and FrameExecution only borrow this context. They must not
/// acquire ownership of any member below or write frame/job state into it.
class RuntimeContext {
public:
    explicit RuntimeContext(chronon3d::Config config);
    ~RuntimeContext();

    RuntimeContext(const RuntimeContext&) = delete;
    RuntimeContext& operator=(const RuntimeContext&) = delete;
    RuntimeContext(RuntimeContext&&) = delete;
    RuntimeContext& operator=(RuntimeContext&&) = delete;

    [[nodiscard]] const chronon3d::Config& config() const noexcept {
        return m_config;
    }

protected:
    chronon3d::Config m_config;
    chronon3d::graph::PipelineCatalogs m_catalogs;
    chronon3d::AssetRegistry m_assets;
    chronon3d::assets::AssetResolver m_resolver;
    chronon3d::assets::MeshPreparationCache m_mesh_cache{};
    RenderSurfaceRegistry m_surface_registry{};
    mutable std::mutex m_prepared_assets_mutex;
    std::shared_ptr<const PreparedAssets> m_prepared_assets{};
    std::string m_prepared_mesh_manifest_key;

    chronon3d::cache::CacheDiagnostics m_diagnostics{};
    chronon3d::cache::NodeCache m_owned_node_cache{};
    std::shared_ptr<chronon3d::cache::FramebufferPool> m_owned_framebuffer_pool;
    chronon3d::graph::CompiledGraphCache m_owned_graph_cache{};
    std::unique_ptr<chronon3d::graph::GraphExecutor> m_owned_executor;
    std::unique_ptr<chronon3d::graph::GraphNodeCatalog> m_owned_graph_node_registry;
    std::unique_ptr<chronon3d::effects::EffectCatalog> m_owned_effect_catalog;
    std::unique_ptr<chronon3d::ExecutionScheduler> m_scheduler;

    chronon3d::ImageCache m_image_cache;
    chronon3d::CurveCache m_curve_cache;
    std::unique_ptr<chronon3d::graph::RenderBackend> m_backend;
    GpuAssetCache m_gpu_asset_cache{};
    GpuGlyphAtlas m_gpu_glyph_atlas{};
    GpuStyledGlyphCache m_gpu_styled_glyph_cache{};
    OverlayTemplateCache m_overlay_template_cache{};
    GpuRuntime m_gpu_runtime{};
    MediaSessionPool m_media_sessions{};
    std::unique_ptr<chronon3d::FontEngine> m_font_engine_owned;
    bool m_populated{false};
};

}  // namespace chronon3d::runtime
