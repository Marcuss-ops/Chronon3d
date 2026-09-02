#include <doctest/doctest.h>

#include <chronon3d/render_graph/compiler/segment_execution_descriptor.hpp>
#include <chronon3d/render_graph/nodes/render_graph_node.hpp>

#include <memory>
#include <string>
#include <utility>

using namespace chronon3d;
using namespace chronon3d::graph;

namespace {

class SegmentTemporalNode final : public RenderGraphNode {
public:
    SegmentTemporalNode(std::string name, TemporalRequirements requirements)
        : RenderGraphNode(no_cache("segment-temporal-test")),
          m_name(std::move(name)),
          m_requirements(requirements) {}

    RenderGraphNodeKind kind() const noexcept override {
        return RenderGraphNodeKind::Source;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return m_name;
    }

    [[nodiscard]] TemporalRequirements temporal_requirements() const noexcept override {
        return m_requirements;
    }

    [[nodiscard]] cache::NodeCacheKey cache_key(const RenderGraphContext&) const override {
        return cache::NodeCacheKey{.scope = m_name};
    }

    NodeExecResult execute(
        RenderGraphContext&,
        std::span<const FramebufferRef>,
        std::span<const std::optional<raster::BBox>>) override {
        return OwnedFB{};
    }

private:
    std::string m_name;
    TemporalRequirements m_requirements{};
};

CompiledFrameGraph make_compiled(
    TemporalRequirements first,
    TemporalRequirements second = {},
    bool second_reachable = true) {
    RenderGraph graph;
    const auto a = graph.add_node(std::make_unique<SegmentTemporalNode>("a", first));
    const auto b = graph.add_node(std::make_unique<SegmentTemporalNode>("b", second));
    graph.connect(a, b);
    graph.set_output(b);
    graph.freeze();

    CompiledFrameGraph compiled;
    compiled.output = b;
    compiled.structure_hash = 0xA55A1234ULL;
    compiled.nodes.resize(2);
    compiled.nodes[a].id = a;
    compiled.nodes[a].reachable = true;
    compiled.nodes[b].id = b;
    compiled.nodes[b].reachable = second_reachable;
    compiled.graph = std::move(graph);
    compiled.valid = true;
    return compiled;
}

} // namespace

TEST_CASE("segment descriptor is frame-local when graph has no temporal dependency") {
    auto compiled = make_compiled({}, {});
    const auto descriptor = compile_segment_execution_descriptor(
        compiled, TimeRange{Frame{100}, Frame{120}}, FrameRate{24, 1},
        std::nullopt, 0xBEEF, 7);

    CHECK(descriptor.valid());
    CHECK(descriptor.preroll == Frame{0});
    CHECK(descriptor.postroll == Frame{0});
    CHECK(descriptor.seek_anchor == Frame{100});
    CHECK(descriptor.required_range().start == Frame{100});
    CHECK(descriptor.required_range().end == Frame{120});
    CHECK(descriptor.decode_range().start == Frame{100});
    CHECK(descriptor.plan_hash == compiled.structure_hash);
    CHECK(descriptor.asset_manifest_hash == 0xBEEF);
    CHECK(descriptor.seed == 7);
}

TEST_CASE("segment descriptor aggregates max frame and exact duration halos") {
    TemporalRequirements first{
        .history_frames = 2,
        .future_frames = 1,
        .history_duration = RationalTime{1, Rational{1, 2}}, // 0.5 s -> 12 frames at 24 fps
        .future_duration = RationalTime{0, Rational{1, 1}},
    };
    TemporalRequirements second{
        .history_frames = 7,
        .future_frames = 2,
        .history_duration = RationalTime{1, Rational{1, 4}}, // 0.25 s -> 6 frames
        .future_duration = RationalTime{1, Rational{1, 10}}, // 0.1 s -> ceil(2.4) = 3
    };
    auto compiled = make_compiled(first, second);

    const auto descriptor = compile_segment_execution_descriptor(
        compiled, TimeRange{Frame{100}, Frame{120}}, FrameRate{24, 1});

    CHECK(descriptor.preroll == Frame{12});
    CHECK(descriptor.postroll == Frame{3});
    CHECK(descriptor.required_range().start == Frame{88});
    CHECK(descriptor.required_range().end == Frame{123});
    CHECK(descriptor.seek_anchor == Frame{88});
}

TEST_CASE("segment descriptor clamps required preroll at timeline start") {
    auto compiled = make_compiled(TemporalRequirements{.history_frames = 5});
    const auto descriptor = compile_segment_execution_descriptor(
        compiled, TimeRange{Frame{2}, Frame{10}}, FrameRate{30, 1});

    CHECK(descriptor.preroll == Frame{5});
    CHECK(descriptor.required_range().start == Frame{0});
    CHECK(descriptor.seek_anchor == Frame{0});
    CHECK(descriptor.decode_range() == TimeRange{Frame{0}, Frame{10}});
}

TEST_CASE("segment descriptor accepts earlier GOP anchor and rejects a late one") {
    auto compiled = make_compiled(TemporalRequirements{.history_frames = 4});

    const auto descriptor = compile_segment_execution_descriptor(
        compiled, TimeRange{Frame{20}, Frame{30}}, FrameRate{30, 1}, Frame{10});
    CHECK(descriptor.required_range().start == Frame{16});
    CHECK(descriptor.seek_anchor == Frame{10});
    CHECK(descriptor.decode_range().start == Frame{10});

    CHECK_THROWS_AS(
        compile_segment_execution_descriptor(
            compiled, TimeRange{Frame{20}, Frame{30}}, FrameRate{30, 1}, Frame{17}),
        std::invalid_argument);
}

TEST_CASE("segment descriptor ignores unreachable temporal nodes") {
    TemporalRequirements unreachable{
        .history_frames = 90,
        .future_frames = 60,
    };
    auto compiled = make_compiled({}, unreachable, false);
    const auto descriptor = compile_segment_execution_descriptor(
        compiled, TimeRange{Frame{50}, Frame{60}}, FrameRate{30000, 1001});

    CHECK(descriptor.preroll == Frame{0});
    CHECK(descriptor.postroll == Frame{0});
}

TEST_CASE("segment descriptor requires explicit valid frame rate for duration conversion") {
    auto compiled = make_compiled(TemporalRequirements{
        .history_duration = RationalTime{1, Rational{1, 2}},
    });

    CHECK_THROWS_AS(
        compile_segment_execution_descriptor(
            compiled, TimeRange{Frame{10}, Frame{20}}, FrameRate{0, 1}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        compile_segment_execution_descriptor(
            compiled, TimeRange{Frame{-1}, Frame{20}}, FrameRate{24, 1}),
        std::invalid_argument);
}
