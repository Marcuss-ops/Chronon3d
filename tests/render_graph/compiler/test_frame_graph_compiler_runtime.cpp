#include "test_frame_graph_compiler_fixtures.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

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

TEST_CASE("CompiledFrameProgram: layer batch index is built with compiled program") {
    CompiledFrameProgram program;
    program.layer_batches.push_back(CompiledLayerBatch{
        .member_nodes = {1, 3}, .root_node = 3, .is_gpu_fused = true});
    program.rebuild_layer_batch_index(4);
    REQUIRE(program.layer_batch_for(3) != nullptr);
    CHECK(program.layer_batch_for(3)->root_node == 3);
    CHECK(program.layer_batch_for(1) != nullptr);
    CHECK(program.layer_batch_for(0) == nullptr);
}

TEST_CASE("CompiledFrameProgram: rejects empty and inconsistent layer batches") {
    SUBCASE("empty batch") {
        CompiledFrameProgram program;
        program.layer_batches.push_back(CompiledLayerBatch{
            .root_node = 0, .is_gpu_fused = true});
        CHECK_THROWS_WITH(
            program.rebuild_layer_batch_index(1),
            doctest::Contains("has no members"));
    }
    SUBCASE("root is not a member") {
        CompiledFrameProgram program;
        program.layer_batches.push_back(CompiledLayerBatch{
            .member_nodes = {1}, .root_node = 0, .is_gpu_fused = true});
        CHECK_THROWS_WITH(
            program.rebuild_layer_batch_index(2),
            doctest::Contains("root is not a member"));
    }
}

TEST_CASE("CompiledFrameProgram: rejects overlapping layer batch ownership") {
    CompiledFrameProgram program;
    program.layer_batches.push_back(CompiledLayerBatch{
        .member_nodes = {2}, .root_node = 2, .is_gpu_fused = true});
    program.layer_batches.push_back(CompiledLayerBatch{
        .member_nodes = {2}, .root_node = 2, .is_gpu_fused = true});
    CHECK_THROWS_WITH(
        program.rebuild_layer_batch_index(4),
        doctest::Contains("overlapping fused batches"));
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

// Contract: image layers execute on SourceNode's native image path and are
// NEVER members of the fused storage-image batch.  Every reachable image source
// carries scene-binding metadata (frame_graph_builder_metadata_detail.hpp sets
// binding_meta.active for BOTH has_layer_location and is_root_source), and
// is_fusible_layer_node() rejects every image source that has a binding.  Batch
// fusion therefore survives only for TextRun sources and the Transform nodes
// that ride along with them.
//
// The retired contract was "100 Image layers lower into 1 CompiledLayerBatch
// and 0 legacy ops".  Evidence for the retirement and the demolition record of
// the retired image path live in
// docs/tickets/TICKET-SCALE-P0-ASSET-VRAM-GOP.md.
TEST_CASE("Invariant: 100 Image layers stay on the native path and are never fused") {
    using namespace chronon3d::graph;

    RenderGraph graph;
    auto clear_node = std::make_unique<ClearNode>();
    GraphNodeId current = graph.add_node(std::move(clear_node));

    std::vector<GraphNodeId> image_nodes;
    image_nodes.reserve(100);
    cache::NodeCacheKey key{.scope = "img", .frame = 0, .width = 1920, .height = 1080};
    for (int i = 0; i < 100; ++i) {
        RenderNode rn;
        rn.shape = Shape{ImageShape{.path = "assets/images/camera_reference.jpg", .size = Vec2{160.0f, 90.0f}}};
        auto img_node = std::make_unique<SourceNode>("img_" + std::to_string(i), rn, key);
        GraphNodeId img_id = graph.add_node(std::move(img_node));
        image_nodes.push_back(img_id);

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
    ctx.backend = &backend;
    auto compiled = compiler.compile(std::move(graph), ctx);

    CHECK(compiled.valid);

    // No batch is emitted for image layers: the fused storage-image path is
    // retired, so the batch list must be EMPTY rather than one 100-instance
    // batch.  Asserting `empty()` also makes the former out-of-bounds access on
    // element 0 — which used to SIGSEGV the whole binary and hide the rest of
    // the suite — impossible by construction.
    CHECK(compiled.program.layer_batches.empty());

    // "Not fused" must never mean "dropped": every image layer still owns at
    // least one execution operation and none of them is marked fused.
    std::vector<bool> node_has_operation(compiled.nodes.size(), false);
    for (const auto& op : compiled.program.operations) {
        if (op.node < compiled.nodes.size()) node_has_operation[op.node] = true;
    }
    std::size_t covered_image_layers = 0;
    for (const GraphNodeId image_id : image_nodes) {
        REQUIRE(image_id < compiled.nodes.size());
        CHECK_FALSE(compiled.nodes[image_id].lowered_into_batch);
        if (node_has_operation[image_id]) ++covered_image_layers;
    }
    CHECK(covered_image_layers == 100);

    // The graph output (the outermost Composite) must still be executed, so
    // "no batch" cannot mean a silently dead program.
    REQUIRE(current < compiled.nodes.size());
    CHECK(node_has_operation[current]);

    // With an empty batch list nothing else may own a node, so every operation
    // must be standalone — including the transforms and composites that the
    // retired contract absorbed into the single 100-instance batch (which is
    // exactly why the retired test asserted 0 legacy ops).
    //
    // Deliberately NOT asserting an exact transform/composite op count: one
    // compile-time pass-through (the zero-translation identity transform of
    // layer 0) does not emit its own operation, so a hardcoded 100 would pin an
    // incidental fixture property rather than this contract.
    for (const auto& op : compiled.program.operations) {
        CHECK_FALSE(op.is_fused);
    }
}
