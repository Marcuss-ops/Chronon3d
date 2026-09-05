#include "test_frame_graph_compiler_fixtures.hpp"

TEST_CASE("ExecutionWorkspaceRing - leases and releases workspace slots deterministically") {
    ExecutionWorkspaceRing ring;

    // Acquire slot 0
    {
        auto lease0 = ring.acquire(0);
        auto& ws0 = lease0.workspace();
        ws0.temp.resize(4);
        CHECK(ws0.temp.size() == 4);

        // Nested frame in-flight leases slot 1 without modifying slot 0
        {
            auto lease1 = ring.acquire(1);
            auto& ws1 = lease1.workspace();
            CHECK(ws1.temp.empty()); // fresh frame begin
            ws1.temp.resize(2);
            CHECK(ws1.temp.size() == 2);
        }
    }

    // After leases go out of scope, slots can be re-acquired cleanly
    auto lease0_again = ring.acquire(0);
    CHECK(lease0_again.workspace().temp.empty()); // begin_frame cleared it
}

TEST_CASE("FrameParameterTable and FrameParameterSampler - sample and retrieve opaque parameters") {
    struct TestParamBlock {
        int64_t source_frame{0};
        float opacity{1.0f};
        float transform[4]{1.0f, 0.0f, 0.0f, 1.0f};
    };

    FrameParameterTable table;
    const std::size_t kFrameCount = 10;
    const Frame kStartFrame{100};

    FrameParameterSampler::prepare(table, kStartFrame, kFrameCount, [](Frame frame, FrameParameterWriter& writer) {
        TestParamBlock block;
        block.source_frame = frame.integral();
        block.opacity = static_cast<float>(frame.integral()) / 100.0f;
        writer.write(block);
    });

    REQUIRE(table.frame_count() == kFrameCount);

    // Verify sample at frame 105
    const auto view = table.view(Frame{105});
    REQUIRE(view.size() == sizeof(TestParamBlock));

    TestParamBlock retrieved{};
    std::memcpy(&retrieved, view.data(), sizeof(TestParamBlock));
    CHECK(retrieved.source_frame == 105);
    CHECK(retrieved.opacity == doctest::Approx(1.05f));
}

#include <chronon3d/runtime/gpu_runtime.hpp>
#include <chronon3d/runtime/media_session_pool.hpp>
#include <chronon3d/runtime/async_encoder_sink.hpp>

TEST_CASE("MediaSessionPool: key hashing and registration") {
    using namespace chronon3d::runtime;
    MediaSessionPool pool;
    CHECK(pool.cached_session_count() == 0);

    MediaSessionKey key{
        MediaCodecId::H264,
        1920,
        1080,
        MediaPixelFormat::NV12,
        0,
        false
    };

    pool.register_session(key, ReusableMediaSession{100, 200, 1, false});
    CHECK(pool.cached_session_count() == 1);

    pool.clear();
    CHECK(pool.cached_session_count() == 0);
}

TEST_CASE("AsyncEncoderSink: asynchronous task dispatch and drain") {
    using namespace chronon3d::runtime;
    std::vector<std::uint64_t> drained_frames;
    std::mutex mtx;

    {
        AsyncEncoderSink sink([&](FrameExecutionSlot* slot, std::uint64_t frame_idx, bool is_flush) {
            (void)slot;
            if (!is_flush) {
                std::lock_guard lock(mtx);
                drained_frames.push_back(frame_idx);
            }
        });

        FrameExecutionSlot s0{}, s1{};
        sink.submit_frame(&s0, 42);
        sink.submit_frame(&s1, 43);
        sink.flush_and_join();
    }

    CHECK(drained_frames.size() == 2);
    CHECK(drained_frames[0] == 42);
    CHECK(drained_frames[1] == 43);
}

TEST_CASE("ProcessorCapabilities: bitmask and fusion predicates") {
    using namespace chronon3d::renderer;
    ProcessorCapabilities caps{};
    CHECK(!caps.is_gpu_fusible());

    caps.gpu = true;
    caps.fusible = true;
    caps.pixel_local = true;
    CHECK(caps.is_gpu_fusible());

    caps.in_place = true;
    caps.native_surface_input = true;
    caps.native_surface_output = true;
    CHECK(caps.is_gpu_fusible());
}

TEST_CASE("CompiledFrameProgram: operation capabilities and fusion properties") {
    using namespace chronon3d::graph;
    using namespace chronon3d::renderer;

    CompiledOperation op{};
    op.node = 5;
    op.capabilities.gpu = true;
    op.capabilities.fusible = true;
    op.capabilities.pixel_local = true;
    op.is_fused = true;

    CHECK(op.capabilities.is_gpu_fusible());
    CHECK(op.is_fused);

    CompiledFrameProgram program{};
    program.levels = {{5}};
    program.operations.push_back(op);
    program.has_fused_passes = true;
    CHECK(!program.empty());
    CHECK(program.has_fused_passes);
}

TEST_CASE("Invariant: 100 Image layers lower into 1 CompiledLayerBatch and 0 legacy ops") {
    using namespace chronon3d::graph;

    RenderGraph graph;
    auto clear_node = std::make_unique<ClearNode>();
    GraphNodeId current = graph.add_node(std::move(clear_node));

    cache::NodeCacheKey key{.scope = "img", .frame = 0, .width = 1920, .height = 1080};
    for (int i = 0; i < 100; ++i) {
        RenderNode rn;
        rn.shape = Shape{ImageShape{.path = "assets/images/camera_reference.jpg", .size = Vec2{160.0f, 90.0f}}};
        auto img_node = std::make_unique<SourceNode>("img_" + std::to_string(i), rn, key);
        GraphNodeId img_id = graph.add_node(std::move(img_node));

        Transform t{Vec3{static_cast<float>(i * 10), static_cast<float>(i * 5), 0.0f}};
        auto xform_node = std::make_unique<TransformNode>(t);
        GraphNodeId xform_id = graph.add_node(std::move(xform_node));
        graph.connect(img_id, xform_id);

        auto comp_node = std::make_unique<CompositeNode>(
            graph.next_composite_id(), BlendMode::Normal);
        GraphNodeId comp_id = graph.add_node(std::move(comp_node));
        graph.connect(current, comp_id);
        graph.connect(xform_id, comp_id);
        current = comp_id;
    }
    graph.set_output(current);

    FrameGraphCompiler compiler;
    ValidationBackend backend(false);
    RenderGraphContext ctx;
    ctx.services.backend = &backend;
    auto compiled = compiler.compile(std::move(graph), ctx);

    CHECK(compiled.program.layer_batches.size() == 1);
    CHECK(compiled.program.layer_batches[0].instances.size() == 100);

    auto count_legacy_transform_ops = [](const CompiledFrameGraph& g) {
        std::size_t count = 0;
        for (const auto& op : g.program.operations) {
            if (op.node < g.nodes.size() && g.nodes[op.node].kind == RenderGraphNodeKind::Transform) {
                count++;
            }
        }
        return count;
    };

    auto count_legacy_composite_ops = [](const CompiledFrameGraph& g) {
        std::size_t count = 0;
        for (const auto& op : g.program.operations) {
            if (!op.is_fused && op.node < g.nodes.size() && g.nodes[op.node].kind == RenderGraphNodeKind::Composite) {
                count++;
            }
        }
        return count;
    };

    CHECK(count_legacy_transform_ops(compiled) == 0);
    CHECK(count_legacy_composite_ops(compiled) == 0);
}
