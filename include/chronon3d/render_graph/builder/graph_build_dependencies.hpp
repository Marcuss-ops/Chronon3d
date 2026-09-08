#pragma once

#include <cstdint>
#include <memory>

namespace chronon3d {
class CompositionRegistry;
class Scene;
namespace media { class MediaFrameProvider; }
namespace effects { class EffectCatalog; }
namespace runtime { struct PreparedAssets; }
namespace graph {
class GraphNodeCatalog;
class LayerTransitionCatalog;
class PrecompBuilderService;
}
}

namespace chronon3d::graph {

struct RenderGraphContext;

/// Immutable dependencies used while lowering an authoring scene into a
/// render graph. This is deliberately separate from frame execution state:
/// a graph build may consume catalogs and prepared assets, while node
/// execution consumes only the compiled result and FrameExecution resources.
struct GraphBuildDependencies {
    const GraphNodeCatalog* node_catalog{nullptr};
    const effects::EffectCatalog* effect_catalog{nullptr};
    const LayerTransitionCatalog* transition_catalog{nullptr};
    const PrecompBuilderService* precomp_builder{nullptr};
    const CompositionRegistry* composition_registry{nullptr};
    media::MediaFrameProvider* video_decoder{nullptr};
    std::shared_ptr<const runtime::PreparedAssets> prepared_assets;
    std::uint64_t registry_generation{0};
};

[[nodiscard]] GraphBuildDependencies make_graph_build_dependencies(
    const RenderGraphContext& context, const Scene& scene);

}  // namespace chronon3d::graph
