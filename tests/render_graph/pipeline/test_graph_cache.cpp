#include <doctest/doctest.h>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/render_graph/pipeline/scene_refresh.hpp>
#include <chronon3d/render_graph/layer/layer_resolver.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <tests/helpers/test_utils.hpp>
#include "src/render_graph/pipeline/helpers.hpp"
#include "src/render_graph/pipeline/scene_context_setup.hpp"
#include <algorithm>

using namespace chronon3d;

using namespace chronon3d::graph;

// Helper: render a single frame and return the framebuffer.
// Uses non-consecutive frames and disables dirty rects so that the
// pre-existing fast-paths (resolved_scene_reuse / fast_path_reuse) do
// NOT swallow the frame before the graph cache is exercised.
static std::shared_ptr<Framebuffer> render_frame(
    SoftwareRenderer& renderer,
    cache::NodeCache& node_cache,
    const Scene& scene,
    const Camera& camera,
    Frame frame,
    int w = 100,
    int h = 100
) {
    return render_scene_via_graph(
    renderer.backend(),
        node_cache,
        scene,
        camera,
        w, h,
        frame, 0.0f,
        renderer.render_settings(),
        renderer.composition_registry(),
        renderer.video_decoder(),
        30.0f,
        {},
        &renderer
    );
}

TEST_CASE("GraphCache - cache hit on structurally identical frames") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    // Disable dirty rects so fast_path_reuse does not trigger.
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Frame 0 — cold start, graph cache miss (no prev fingerprint)
    auto fb0 = render_frame(renderer, node_cache, scene, camera, Frame{0});
    REQUIRE(fb0 != nullptr);

    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    const auto misses_before = renderer.counters()->graph_cache_misses.load();

    // Frame 2 (non-consecutive) — structurally identical scene → graph cache hit.
    // Frame 1 would be caught by resolved_scene_reuse, so we skip it.
    auto fb1 = render_frame(renderer, node_cache, scene, camera, Frame{2});
    REQUIRE(fb1 != nullptr);

    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before);
}

TEST_CASE("GraphCache - cache miss when dimensions change") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Warm-up frame 0
    render_frame(renderer, node_cache, scene, camera, Frame{0});

    // Frame 2 with same scene → should be a graph cache hit
    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    render_frame(renderer, node_cache, scene, camera, Frame{2});
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);

    // Frame 4 with different dimensions → graph topology changes (clip rects)
    auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto fb_diff_size = render_frame(renderer, node_cache, scene, camera, Frame{4}, 200, 200);
    REQUIRE(fb_diff_size != nullptr);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);
}

TEST_CASE("GraphCache - refresh reuses topology for dynamic source changes") {
    SceneBuilder builder_a;
    builder_a.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene_a = builder_a.build();

    SceneBuilder builder_b;
    builder_b.rect("r", {.size={50.0f, 50.0f}, .color=Color::blue(), .pos={17.0f, 11.0f, 0.0f}});
    Scene scene_b = builder_b.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    render_frame(renderer, node_cache, scene_a, camera, Frame{0});
    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    const auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto refreshed = render_frame(renderer, node_cache, scene_b, camera, Frame{2});

    REQUIRE(refreshed != nullptr);
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before);
}

TEST_CASE("GraphCache - successful refresh changes only dynamic source state") {
    SceneBuilder builder_a;
    builder_a.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    const Scene scene_a = builder_a.build();

    SceneBuilder builder_b;
    builder_b.rect("r", {.size={50.0f, 50.0f}, .color=Color::blue(), .pos={17.0f, 11.0f, 0.0f}});
    const Scene scene_b = builder_b.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    REQUIRE(render_frame(renderer, node_cache, scene_a, camera, Frame{0}) != nullptr);
    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());

    GraphNodeId source_id = k_invalid_node;
    for (GraphNodeId id = 0; id < cached->graph.size(); ++id) {
        if (!cached->graph.has_node(id)) continue;
        const auto* source = dynamic_cast<const SourceNode*>(&cached->graph.node(id));
        if (source && source->name() == "r") {
            source_id = id;
            break;
        }
    }
    REQUIRE(source_id != k_invalid_node);

    const auto* before = dynamic_cast<const SourceNode*>(&cached->graph.node(source_id));
    REQUIRE(before != nullptr);
    const auto before_position = before->render_node().world_transform.position;
    const auto before_kind = before->kind();
    const auto before_name = std::string(before->name());
    const auto before_layer_id = std::string(before->layer_id());
    const auto before_inputs = cached->graph.inputs(source_id);
    const auto before_info = cached->nodes[source_id];
    const auto before_consumers = before_info.consumers;
    const auto before_policy = before_info.cache_policy;

    auto refresh_ctx = make_graph_context(
        renderer.backend(), node_cache, camera, 100, 100, Frame{2}, 0.0f,
        renderer.render_settings(), renderer.composition_registry(),
        renderer.video_decoder(), 30.0f);
    chronon3d::graph::detail::setup_render_graph_context(refresh_ctx, scene_b, &renderer);
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene_b, refresh_ctx);
    const auto result = chronon3d::graph::detail::refresh_compiled_graph_payloads(
        *cached, scene_b, refresh_ctx, resolved);

    REQUIRE(result);
    const auto* after = dynamic_cast<const SourceNode*>(&cached->graph.node(source_id));
    REQUIRE(after != nullptr);
    CHECK(after->render_node().world_transform.position.x == 17.0f);
    CHECK(after->render_node().world_transform.position.y == 11.0f);
    CHECK(after->render_node().world_transform.position != before_position);
    CHECK(after->kind() == before_kind);
    CHECK(after->name() == before_name);
    CHECK(after->layer_id() == before_layer_id);
    CHECK(cached->graph.inputs(source_id) == before_inputs);
    CHECK(cached->nodes[source_id].consumers == before_consumers);
    CHECK(cached->nodes[source_id].id == before_info.id);
    CHECK(cached->nodes[source_id].kind == before_info.kind);
    CHECK(cached->nodes[source_id].name == before_info.name);
    CHECK(cached->nodes[source_id].layer_id == before_info.layer_id);
    CHECK(cached->nodes[source_id].processor_id == before_info.processor_id);
    CHECK(cached->nodes[source_id].shape_type == before_info.shape_type);
    CHECK(cached->nodes[source_id].source_shape_types == before_info.source_shape_types);
    CHECK(cached->nodes[source_id].cache_policy.mode == before_policy.mode);
    CHECK(cached->nodes[source_id].cache_policy.invalidation == before_policy.invalidation);
    CHECK(cached->nodes[source_id].cache_policy.reason == before_policy.reason);
}

TEST_CASE("GraphCache - cache miss when renderable shape topology changes") {
    SceneBuilder rect_builder;
    rect_builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene rect_scene = rect_builder.build();

    SceneBuilder circle_builder;
    circle_builder.circle("r", {.radius=25.0f, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene circle_scene = circle_builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    render_frame(renderer, node_cache, rect_scene, camera, Frame{0});
    const auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto rebuilt = render_frame(renderer, node_cache, circle_scene, camera, Frame{2});

    REQUIRE(rebuilt != nullptr);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);
}

TEST_CASE("GraphCache - topology mismatch rebuilds and republishes a valid graph") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    auto first = render_frame(renderer, node_cache, scene, camera, Frame{0});
    REQUIRE(first != nullptr);

    // Corrupt only the cached structural metadata. The authored scene remains
    // unchanged, so the coordinator enters the refresh path; scene_refresh
    // must reject the candidate, restore the checked-out entry, and compile a
    // fresh graph instead of publishing a partially refreshed graph.
    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());
    REQUIRE_FALSE(cached->nodes.empty());
    auto node_it = std::find_if(cached->nodes.begin(), cached->nodes.end(),
        [](const CompiledNodeInfo& info) { return info.reachable; });
    REQUIRE(node_it != cached->nodes.end());
    node_it->processor_id = "processor.mismatch";
    renderer.graph_cache().store(std::move(*cached), 100, 100);

    const auto hits_before = renderer.counters()->graph_cache_hits.load();
    const auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto rebuilt = render_frame(renderer, node_cache, scene, camera, Frame{2});

    REQUIRE(rebuilt != nullptr);
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);

    // Compare the mismatch fallback with a genuinely cold renderer. This
    // proves that the rejected candidate did not publish a partially
    // refreshed graph whose output would only happen to be non-null.
    auto cold_renderer = test::make_renderer();
    auto cold_settings = cold_renderer.render_settings();
    cold_settings.dirty.enabled = false;
    cold_renderer.set_settings(cold_settings);
    cache::NodeCache cold_node_cache;
    auto cold_reference = render_frame(
        cold_renderer, cold_node_cache, scene, camera, Frame{2});
    REQUIRE(cold_reference != nullptr);
    CHECK(test::framebuffer_hash(*rebuilt) ==
          test::framebuffer_hash(*cold_reference));

    REQUIRE(renderer.graph_cache().has(100, 100));

    auto repaired = renderer.graph_cache().try_take(100, 100);
    REQUIRE(repaired.has_value());
    const auto repaired_node = std::find_if(repaired->nodes.begin(), repaired->nodes.end(),
        [](const CompiledNodeInfo& info) { return info.reachable; });
    REQUIRE(repaired_node != repaired->nodes.end());
    CHECK(repaired_node->processor_id != "processor.mismatch");
    renderer.graph_cache().store(std::move(*repaired), 100, 100);
}

TEST_CASE("GraphCache - failed refresh leaves every cached node unchanged") {
    SceneBuilder builder_a;
    builder_a.rect("a", {.size={30.0f, 30.0f}, .color=Color::red(), .pos={10.0f, 12.0f, 0.0f}});
    builder_a.rect("b", {.size={24.0f, 24.0f}, .color=Color::blue(), .pos={42.0f, 18.0f, 0.0f}});
    const Scene scene_a = builder_a.build();

    SceneBuilder builder_b;
    builder_b.rect("a", {.size={30.0f, 30.0f}, .color=Color::red(), .pos={710.0f, 612.0f, 0.0f}});
    builder_b.rect("b", {.size={24.0f, 24.0f}, .color=Color::blue(), .pos={742.0f, 618.0f, 0.0f}});
    const Scene scene_b = builder_b.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    auto first = render_frame(renderer, node_cache, scene_a, camera, Frame{0});
    REQUIRE(first != nullptr);

    // Detach the cached candidate exactly as graph_cache_coordinator does
    // before attempting a refresh. Corrupt a later metadata entry so the
    // complete structural validation fails after the first source node would
    // have been prepared by a non-transactional implementation.
    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());
    REQUIRE(cached->graph.phase() == GraphPhase::Building);

    GraphNodeId source_id = k_invalid_node;
    for (GraphNodeId id = 0; id < cached->graph.size(); ++id) {
        if (!cached->graph.has_node(id)) continue;
        const auto* source = dynamic_cast<const SourceNode*>(&cached->graph.node(id));
        if (source && source->name() == "a") {
            source_id = id;
            break;
        }
    }
    REQUIRE(source_id != k_invalid_node);

    const auto* source_before =
        dynamic_cast<const SourceNode*>(&cached->graph.node(source_id));
    REQUIRE(source_before != nullptr);
    const auto old_position = source_before->render_node().world_transform.position;

    auto later_metadata = std::find_if(
        cached->nodes.begin(), cached->nodes.end(),
        [source_id](const CompiledNodeInfo& info) {
            return info.reachable && info.id > source_id;
        });
    REQUIRE(later_metadata != cached->nodes.end());
    later_metadata->processor_id += ".forced-refresh-failure";
    const auto forced_processor_id = later_metadata->processor_id;
    const auto later_node_id = later_metadata->id;

    auto refresh_ctx = make_graph_context(
        renderer.backend(), node_cache, camera, 100, 100, Frame{2}, 0.0f,
        renderer.render_settings(), renderer.composition_registry(),
        renderer.video_decoder(), 30.0f);
    chronon3d::graph::detail::setup_render_graph_context(refresh_ctx, scene_b, &renderer);
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene_b, refresh_ctx);

    const auto key_before = source_before->cache_key(refresh_ctx);
    const auto original_inputs = cached->graph.inputs(source_id);
    const auto original_consumers = cached->nodes[source_id].consumers;
    const auto original_processor_id = cached->nodes[source_id].processor_id;
    const auto original_shape_type = cached->nodes[source_id].shape_type;
    const auto original_policy = cached->nodes[source_id].cache_policy;
    const auto refresh_result = chronon3d::graph::detail::refresh_compiled_graph_payloads(
        *cached, scene_b, refresh_ctx, resolved);

    CHECK_FALSE(refresh_result);
    CHECK(refresh_result.status ==
          chronon3d::graph::detail::SceneRefreshStatus::TopologyMismatch);

    // The scene deliberately moves both source nodes. A refresh that mutates
    // the graph while validating would update source "a" before discovering
    // the corrupted later metadata. The transaction must leave the detached
    // candidate byte-for-byte equivalent at the node-payload boundary so the
    // coordinator can restore it to the cache unchanged.
    const auto* source_after =
        dynamic_cast<const SourceNode*>(&cached->graph.node(source_id));
    REQUIRE(source_after != nullptr);
    CHECK(source_after->render_node().world_transform.position.x == old_position.x);
    CHECK(source_after->render_node().world_transform.position.y == old_position.y);
    CHECK(source_after->cache_key(refresh_ctx) == key_before);
    CHECK(cached->graph.inputs(source_id) == original_inputs);
    CHECK(cached->nodes[source_id].consumers == original_consumers);
    CHECK(cached->nodes[source_id].processor_id == original_processor_id);
    CHECK(cached->nodes[source_id].shape_type == original_shape_type);
    CHECK(cached->nodes[source_id].cache_policy.mode == original_policy.mode);
    CHECK(cached->nodes[source_id].cache_policy.invalidation == original_policy.invalidation);

    // Reinsert the rejected candidate, mirroring the coordinator's restore
    // path, and verify that the original cache entry remains available.
    renderer.graph_cache().store(std::move(*cached), 100, 100);
    CHECK(renderer.graph_cache().has(100, 100));
    auto restored = renderer.graph_cache().try_take(100, 100);
    REQUIRE(restored.has_value());
    const auto* restored_source =
        dynamic_cast<const SourceNode*>(&restored->graph.node(source_id));
    REQUIRE(restored_source != nullptr);
    CHECK(restored_source->render_node().world_transform.position.x == old_position.x);
    CHECK(restored_source->render_node().world_transform.position.y == old_position.y);
    const auto restored_metadata = std::find_if(
        restored->nodes.begin(), restored->nodes.end(),
        [later_node_id](const CompiledNodeInfo& info) {
            return info.id == later_node_id;
        });
    REQUIRE(restored_metadata != restored->nodes.end());
    CHECK(restored_metadata->processor_id == forced_processor_id);
}

TEST_CASE("GraphCache - scene refresh reports missing processor explicitly") {
    SceneBuilder builder;
    builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::red(), .pos={5.0f, 7.0f, 0.0f}});
    const Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;
    REQUIRE(render_frame(renderer, node_cache, scene, camera, Frame{0}) != nullptr);

    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());
    auto processor = std::find_if(cached->nodes.begin(), cached->nodes.end(),
        [](const CompiledNodeInfo& info) { return info.reachable; });
    REQUIRE(processor != cached->nodes.end());
    processor->processor_id.clear();

    auto refresh_ctx = make_graph_context(
        renderer.backend(), node_cache, camera, 100, 100, Frame{2}, 0.0f,
        renderer.render_settings(), renderer.composition_registry(),
        renderer.video_decoder(), 30.0f);
    chronon3d::graph::detail::setup_render_graph_context(refresh_ctx, scene, &renderer);
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, refresh_ctx);
    const auto result = chronon3d::graph::detail::refresh_compiled_graph_payloads(
        *cached, scene, refresh_ctx, resolved);

    CHECK_FALSE(result);
    CHECK(result.status == chronon3d::graph::detail::SceneRefreshStatus::MissingProcessor);
}

TEST_CASE("GraphCache - scene refresh reports invalid renderable node explicitly") {
    SceneBuilder valid_builder;
    valid_builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::red(), .pos={5.0f, 7.0f, 0.0f}});
    const Scene valid_scene = valid_builder.build();

    SceneBuilder invalid_builder;
    invalid_builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::red(), .pos={5.0f, 7.0f, 0.0f}});
    Scene invalid_scene = invalid_builder.build();
    REQUIRE_FALSE(invalid_scene.nodes().empty());
    invalid_scene.nodes().front().shape.set_type(ShapeType::None);

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;
    REQUIRE(render_frame(renderer, node_cache, valid_scene, camera, Frame{0}) != nullptr);

    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());
    auto refresh_ctx = make_graph_context(
        renderer.backend(), node_cache, camera, 100, 100, Frame{2}, 0.0f,
        renderer.render_settings(), renderer.composition_registry(),
        renderer.video_decoder(), 30.0f);
    chronon3d::graph::detail::setup_render_graph_context(refresh_ctx, invalid_scene, &renderer);
    const auto resolved = chronon3d::graph::detail::resolve_layers(invalid_scene, refresh_ctx);
    const auto result = chronon3d::graph::detail::refresh_compiled_graph_payloads(
        *cached, invalid_scene, refresh_ctx, resolved);

    CHECK_FALSE(result);
    CHECK(result.status == chronon3d::graph::detail::SceneRefreshStatus::InvalidRenderableNode);
}

TEST_CASE("GraphCache - scene refresh rejects consumer metadata changes") {
    SceneBuilder builder;
    builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::red(), .pos={5.0f, 7.0f, 0.0f}});
    const Scene scene = builder.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;
    REQUIRE(render_frame(renderer, node_cache, scene, camera, Frame{0}) != nullptr);

    auto cached = renderer.graph_cache().try_take(100, 100);
    REQUIRE(cached.has_value());
    auto metadata = std::find_if(cached->nodes.begin(), cached->nodes.end(),
        [](const CompiledNodeInfo& info) { return info.reachable && !info.consumers.empty(); });
    REQUIRE(metadata != cached->nodes.end());
    metadata->consumers.clear();

    auto refresh_ctx = make_graph_context(
        renderer.backend(), node_cache, camera, 100, 100, Frame{2}, 0.0f,
        renderer.render_settings(), renderer.composition_registry(),
        renderer.video_decoder(), 30.0f);
    chronon3d::graph::detail::setup_render_graph_context(refresh_ctx, scene, &renderer);
    const auto resolved = chronon3d::graph::detail::resolve_layers(scene, refresh_ctx);
    const auto result = chronon3d::graph::detail::refresh_compiled_graph_payloads(
        *cached, scene, refresh_ctx, resolved);

    CHECK_FALSE(result);
    CHECK(result.status == chronon3d::graph::detail::SceneRefreshStatus::TopologyMismatch);
}

TEST_CASE("GraphCache - cache miss when layer added") {
    SceneBuilder builder_a;
    builder_a.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene_a = builder_a.build();

    SceneBuilder builder_b;
    builder_b.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    builder_b.rect("b", {.size={30.0f, 30.0f}, .color=Color::blue(), .pos={10.0f, 10.0f, 0.0f}});
    Scene scene_b = builder_b.build();

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);

    cache::NodeCache node_cache;
    Camera camera;

    // Frame 0 with scene_a
    render_frame(renderer, node_cache, scene_a, camera, Frame{0});

    // Frame 2 with scene_a again → hit
    auto hits_before = renderer.counters()->graph_cache_hits.load();
    render_frame(renderer, node_cache, scene_a, camera, Frame{2});
    CHECK(renderer.counters()->graph_cache_hits.load() == hits_before + 1);

    // Frame 4 with scene_b (extra layer) → miss (different static fingerprint)
    auto misses_before = renderer.counters()->graph_cache_misses.load();
    auto fb_b = render_frame(renderer, node_cache, scene_b, camera, Frame{4});
    REQUIRE(fb_b != nullptr);
    CHECK(renderer.counters()->graph_cache_misses.load() == misses_before + 1);
}

TEST_CASE("GraphCache - failed fresh compile preserves previous cache") {
    SceneBuilder valid_builder;
    valid_builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::red(), .pos={5.0f, 7.0f, 0.0f}});
    const Scene valid_scene = valid_builder.build();

    SceneBuilder invalid_builder;
    invalid_builder.rect("r", {.size={40.0f, 40.0f}, .color=Color::blue(), .pos={25.0f, 27.0f, 0.0f}});
    Scene invalid_scene = invalid_builder.build();
    REQUIRE_FALSE(invalid_scene.nodes().empty());
    invalid_scene.nodes().front().shape.set_type(ShapeType::None);

    auto renderer = test::make_renderer();
    RenderSettings settings = renderer.render_settings();
    settings.dirty.enabled = false;
    renderer.set_settings(settings);
    cache::NodeCache node_cache;
    Camera camera;

    REQUIRE(render_frame(renderer, node_cache, valid_scene, camera, Frame{0}) != nullptr);
    REQUIRE(renderer.graph_cache().has(100, 100));
    auto previous = renderer.graph_cache().try_take(100, 100);
    REQUIRE(previous.has_value());
    REQUIRE(previous->valid);
    const auto previous_instance = previous->graph_instance_id;
    renderer.graph_cache().store(std::move(*previous), 100, 100);

    // A changed scene structure forces a fresh compile. ShapeType::None is
    // rejected by the compiler; the coordinator must not consume the valid
    // graph that was already cached for this resolution.
    CHECK_THROWS(render_frame(renderer, node_cache, invalid_scene, camera, Frame{2}));
    REQUIRE(renderer.graph_cache().has(100, 100));
    auto preserved = renderer.graph_cache().try_take(100, 100);
    REQUIRE(preserved.has_value());
    CHECK(preserved->valid);
    CHECK(preserved->graph_instance_id == previous_instance);
    renderer.graph_cache().store(std::move(*preserved), 100, 100);
}

TEST_CASE("GraphCache - pixel output matches non-cached path") {
    SceneBuilder builder;
    builder.rect("r", {.size={50.0f, 50.0f}, .color=Color::red(), .pos={0.0f, 0.0f, 0.0f}});
    Scene scene = builder.build();

    // --- Cached path: frame 0 builds, frame 2 reuses cached graph ---
    auto renderer_cached = test::make_renderer();
    RenderSettings settings_c = renderer_cached.render_settings();
    settings_c.dirty.enabled = false;
    renderer_cached.set_settings(settings_c);
    cache::NodeCache node_cache_cached;
    Camera camera;

    render_frame(renderer_cached, node_cache_cached, scene, camera, Frame{0});
    auto fb_cached = render_frame(renderer_cached, node_cache_cached, scene, camera, Frame{2});
    REQUIRE(fb_cached != nullptr);
    CHECK(renderer_cached.counters()->graph_cache_hits.load() >= 1);

    // --- Non-cached path: invalidate only the compiled graph ---
    // Preserve framebuffer/session setup while forcing a fresh graph build;
    // clearing the whole renderer also clears the output pool and is not a
    // valid pixel reference for this comparison.
    auto renderer_fresh = test::make_renderer();
    RenderSettings settings_f = renderer_fresh.render_settings();
    settings_f.dirty.enabled = false;
    renderer_fresh.set_settings(settings_f);
    cache::NodeCache node_cache_fresh;
    render_frame(renderer_fresh, node_cache_fresh, scene, camera, Frame{0});
    renderer_fresh.graph_cache().reset();

    auto fb_fresh = render_frame(renderer_fresh, node_cache_fresh, scene, camera, Frame{2});
    REQUIRE(fb_fresh != nullptr);
    CHECK(renderer_fresh.counters()->graph_cache_hits.load() == 0);

    // Compare pixel-for-pixel
    REQUIRE(fb_cached->width() == fb_fresh->width());
    REQUIRE(fb_cached->height() == fb_fresh->height());

    bool matches = true;
    for (int y = 0; y < fb_cached->height(); ++y) {
        for (int x = 0; x < fb_cached->width(); ++x) {
            Color c1 = fb_cached->get_pixel(x, y);
            Color c2 = fb_fresh->get_pixel(x, y);
            if (std::abs(c1.r - c2.r) > 0.01f ||
                std::abs(c1.g - c2.g) > 0.01f ||
                std::abs(c1.b - c2.b) > 0.01f ||
                std::abs(c1.a - c2.a) > 0.01f) {
                matches = false;
                break;
            }
        }
    }
    CHECK(matches);
}
