#pragma once

#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/frame_graph_compiler.hpp>
#include <chronon3d/internal/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/clear_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/composite_node.hpp>
#include <chronon3d/render_graph/nodes/transition_node.hpp>
#include <chronon3d/render_graph/nodes/text_run_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/effects/effect_params.hpp>
#include <chronon3d/render_graph/render_backend.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include <chronon3d/backends/software/software_registry.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/runtime/render_runtime.hpp>
#include <tests/helpers/test_utils.hpp>
#include <chronon3d/scene/model/render/render_node.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <memory>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

class LifetimeShapeProcessor final : public renderer::ShapeProcessor {
public:
    explicit LifetimeShapeProcessor(int& destructions) : m_destructions(&destructions) {}
    ~LifetimeShapeProcessor() override { ++*m_destructions; }

    void draw(const SoftwareProcessorContext&, Framebuffer&, const RenderNode&,
              const RenderState&, const Camera&, i32, i32) override {}
    raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
        return raster::BBox{};
    }
    bool hit_test(const Shape&, Vec2, f32) override { return false; }

private:
    int* m_destructions;
};

class NoopShapeProcessor final : public renderer::ShapeProcessor {
public:
    void draw(const SoftwareProcessorContext&, Framebuffer&, const RenderNode&,
              const RenderState&, const Camera&, i32, i32) override {}
    raster::BBox compute_world_bbox(const Shape&, const Mat4&, f32) override {
        return raster::BBox{};
    }
    bool hit_test(const Shape&, Vec2, f32) override { return false; }
};

class NoopEffectProcessor final : public renderer::EffectProcessor {
public:
    void apply(Framebuffer&, const EffectParams&,
               const effects::EffectExecutionContext&) override {}
};

class ValidationBackend final : public chronon3d::graph::RenderBackend {
public:
    explicit ValidationBackend(
        bool missing_processor,
        std::shared_ptr<const renderer::ProcessorRegistrySnapshot> snapshot = nullptr)
        : m_missing_processor(missing_processor)
        , m_snapshot(snapshot ? std::move(snapshot)
                              : std::make_shared<const renderer::ProcessorRegistrySnapshot>(
                                    std::vector<renderer::ProcessorRegistrySnapshot::ShapeEntry>{
                                        {ShapeType::Rect, std::make_shared<NoopShapeProcessor>()},
                                        {ShapeType::Image, std::make_shared<NoopShapeProcessor>()}},
                                    std::vector<renderer::ProcessorRegistrySnapshot::EffectEntry>{
                                        {std::type_index(typeid(BlurParams)),
                                         std::make_shared<NoopEffectProcessor>()}},
                                    0)) {}

    std::shared_ptr<const renderer::ProcessorRegistrySnapshot>
    processor_snapshot() const noexcept override {
        return m_snapshot;
    }

    bool requires_processor_snapshot() const noexcept override {
        return true;
    }

    std::optional<chronon3d::graph::RenderBackendError> validate_render_node(
        const RenderNode&) const override {
        ++m_validation_calls;
        if (m_missing_processor) {
            return chronon3d::graph::RenderBackendError{
                chronon3d::graph::RenderBackendErrorCode::InvalidInput,
                "missing shape processor (test backend)"};
        }
        return std::nullopt;
    }

    void apply_per_pixel_dof(
        Framebuffer&, std::span<const float>, const DepthOfFieldSettings&,
        const LensModel&, const std::optional<raster::BBox>&) override {}
    void apply_effect_stack(
        Framebuffer&, const EffectStack&,
        const effects::EffectExecutionContext&) override {}
    void composite_layer(
        Framebuffer&, const Framebuffer&, BlendMode,
        const std::optional<raster::BBox>&, CompositeOperator) override {}
    void apply_blur(
        Framebuffer&, float, const std::optional<raster::BBox>&) override {}

    [[nodiscard]] int validation_calls() const noexcept {
        return m_validation_calls;
    }

private:
    bool m_missing_processor{false};
    std::shared_ptr<const renderer::ProcessorRegistrySnapshot> m_snapshot;
    mutable int m_validation_calls{0};
};

class CompilerTestNode final : public RenderGraphNode {
public:
    // PR2-cleanup: cache policy is decided at construction; legacy
    // `bool cache` / `bool frame_dep` ctor args and `m_cacheable` member were dropped.
    explicit CompilerTestNode(std::string n,
                               RenderNodeCachePolicy policy = static_memory_cache("test"))
        : RenderGraphNode(policy), m_name(std::move(n)) {}

    RenderGraphNodeKind kind() const noexcept override { return RenderGraphNodeKind::Source; }
    [[nodiscard]] std::string_view name() const noexcept override { return m_name; }

    std::optional<raster::BBox> predicted_bbox(
        const RenderGraphContext& ctx,
        std::span<const std::optional<raster::BBox>>
    ) const override {
        return raster::BBox{0, 0, ctx.frame_input.width, ctx.frame_input.height};
    }

    cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name, .frame = 0, .width = 0, .height = 0};
    }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>
    ) override {
        return OwnedFB{};
    }

private:
    std::string m_name;
};

inline RenderGraph make_single_source_graph(ShapeType shape_type) {
    RenderGraph graph;
    RenderNode render_node;
    render_node.shape.set_type(shape_type);
    auto source = std::make_unique<SourceNode>(
        "source", render_node, cache::NodeCacheKey{});
    const auto source_id = graph.add_node(std::move(source));
    graph.set_output(source_id);
    return graph;
}

} // namespace
