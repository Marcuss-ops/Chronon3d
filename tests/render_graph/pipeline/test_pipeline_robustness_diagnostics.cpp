#include <doctest/doctest.h>
#include <tests/helpers/doctest_skip_compat.hpp>
#include <spdlog/spdlog.h>
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/cache/node_cache.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/multi_source_node.hpp>
#include <chronon3d/scene/model/render/render_node_factory.hpp>
#include <cmath>
#include <array>
#include <cstdlib>
#include <string>
#include <mutex>
#include "src/render_graph/builder/graph_builder_coordinates.hpp"
#include "src/render_graph/builder/evaluated_layer_placement.hpp"
#include "src/render_graph/builder/graph_builder_internal.hpp"
#include "src/render_graph/executor/tile_pruning.hpp"
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

struct DiagnosticsParityObservation {
    raster::BBox bbox{};
    std::optional<raster::BBox> dirty_clip;
    bool bbox_empty{false};
    u64 pixel_hash{0};
};

bool same_bbox(const raster::BBox& a, const raster::BBox& b) {
    return a.x0 == b.x0 && a.y0 == b.y0 &&
           a.x1 == b.x1 && a.y1 == b.y1;
}

DiagnosticsParityObservation observe_source_diagnostics(
    SoftwareRenderer& renderer,
    const RenderNode& render_node,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.frame_input.frame = 0;
    ctx.policy.diagnostics_enabled = diagnostics_enabled;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = renderer.backend().processor_snapshot();
    REQUIRE(ctx.node_exec.processor_snapshot != nullptr);
    ctx.node_exec.current_shape_processor =
        ctx.node_exec.processor_snapshot->shape_handle(render_node.shape.type());
    if (camera_2_5d) {
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.camera_2_5d.enabled = true;
        ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
        ctx.frame_input.camera_2_5d.zoom = 800.0f;
    }

    SourceNode node("diagnostics_source", render_node, cache::NodeCacheKey{});
    const auto predicted = node.predicted_bbox(ctx);
    REQUIRE(predicted.has_value());

    // Exercise the real executor dirty-clip decision consumed after
    // predicted_bbox(). Source/Text/Transform nodes intentionally preserve
    // their full predicted bounds here; parity proves diagnostics cannot
    // alter that execution decision.
    ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
    const auto dirty_clip = compute_dirty_clip(ctx, node, predicted);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto framebuffer = result.take_value();
    REQUIRE(framebuffer != nullptr);

    return DiagnosticsParityObservation{
        .bbox = *predicted,
        .dirty_clip = dirty_clip,
        .bbox_empty = predicted->is_empty(),
        .pixel_hash = test::framebuffer_hash(*framebuffer),
    };
}

DiagnosticsParityObservation observe_multi_source_diagnostics(
    SoftwareRenderer& renderer,
    const RenderNode& first,
    const RenderNode& second,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    std::vector<MultiSourceItem> items{
        MultiSourceItem{&first, first.world_transform.to_mat4(), 1.0f},
        MultiSourceItem{&second, second.world_transform.to_mat4(), 1.0f},
    };
    const auto snapshot = renderer.backend().processor_snapshot();
    REQUIRE(snapshot != nullptr);
    std::array<renderer::ShapeProcessorHandle, 2> processors{
        snapshot->shape_handle(first.shape.type()),
        snapshot->shape_handle(second.shape.type()),
    };

    RenderGraphContext ctx;
    ctx.frame_input.width = 320;
    ctx.frame_input.height = 240;
    ctx.frame_input.frame = 0;
    ctx.policy.diagnostics_enabled = diagnostics_enabled;
    ctx.services.backend = &renderer.backend();
    ctx.node_exec.processor_snapshot = snapshot;
    ctx.node_exec.current_shape_processor = processors[0];
    ctx.node_exec.current_shape_processors = processors;
    if (camera_2_5d) {
        ctx.frame_input.has_camera_2_5d = true;
        ctx.frame_input.camera_2_5d.enabled = true;
        ctx.frame_input.camera_2_5d.position = {0.0f, 0.0f, -800.0f};
        ctx.frame_input.camera_2_5d.zoom = 800.0f;
    }

    MultiSourceNode node("diagnostics_multi_source", std::move(items), cache::NodeCacheKey{});
    const auto predicted = node.predicted_bbox(ctx);
    REQUIRE(predicted.has_value());

    // Exercise the real executor dirty-clip decision consumed after
    // predicted_bbox(), not just a duplicated test-side intersection.
    ctx.node_exec.dirty_rect = raster::BBox{0, 0, 160, 120};
    const auto dirty_clip = compute_dirty_clip(ctx, node, predicted);

    auto result = node.execute(ctx, {}, {});
    REQUIRE(result.has_value());
    auto framebuffer = result.take_value();
    REQUIRE(framebuffer != nullptr);

    return DiagnosticsParityObservation{
        .bbox = *predicted,
        .dirty_clip = dirty_clip,
        .bbox_empty = predicted->is_empty(),
        .pixel_hash = test::framebuffer_hash(*framebuffer),
    };
}

void check_parity_decision_and_pixels(
    const DiagnosticsParityObservation& off,
    const DiagnosticsParityObservation& on)
{
    // The bbox is the node's culling/tile/dirty decision input. Exact equality
    // plus equality of the real dirty-clip result proves diagnostics cannot
    // change the execution decision, not merely the final pixels.
    CHECK(same_bbox(off.bbox, on.bbox));
    REQUIRE(off.dirty_clip.has_value());
    REQUIRE(on.dirty_clip.has_value());
    CHECK(same_bbox(*off.dirty_clip, *on.dirty_clip));
    CHECK(off.bbox_empty == on.bbox_empty);
    CHECK(off.pixel_hash == on.pixel_hash);
}

} // namespace

namespace {

class SchedulerEnvironment final {
public:
    explicit SchedulerEnvironment(bool parallel)
        : m_lock(environment_mutex())
        , m_mode(capture("CHRONON3D_SCHEDULER_MODE"))
        , m_workers(capture("CHRONON3D_SCHEDULER_WORKERS")) {
        set("CHRONON3D_SCHEDULER_MODE", parallel ? "fixed" : "sequential");
        if (parallel) {
            set("CHRONON3D_SCHEDULER_WORKERS", "4");
        } else {
            unsetenv("CHRONON3D_SCHEDULER_WORKERS");
        }
    }

    ~SchedulerEnvironment() noexcept {
        restore("CHRONON3D_SCHEDULER_MODE", m_mode);
        restore("CHRONON3D_SCHEDULER_WORKERS", m_workers);
    }

    SchedulerEnvironment(const SchedulerEnvironment&) = delete;
    SchedulerEnvironment& operator=(const SchedulerEnvironment&) = delete;

private:
    static std::mutex& environment_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    struct Variable {
        bool present{false};
        std::string value;
    };

    static Variable capture(const char* name) {
        if (const char* value = std::getenv(name)) {
            return Variable{true, value};
        }
        return {};
    }

    static void set(const char* name, const char* value) {
        (void)::setenv(name, value, 1);
    }

    static void restore(const char* name, const Variable& variable) noexcept {
        if (variable.present) {
            (void)::setenv(name, variable.value.c_str(), 1);
        } else {
            (void)::unsetenv(name);
        }
    }

    std::unique_lock<std::mutex> m_lock;
    Variable m_mode;
    Variable m_workers;
};

struct RuntimeDiagnosticsObservation {
    u64 pixel_hash{0};
    bool dirty_rect_enabled{false};
    bool tile_execution_used{false};
    bool fast_path_reused{false};
    bool graph_reused{false};
    int layer_count{0};
    std::optional<raster::BBox> dirty_rect;
    raster::BBox node_bbox{};
    std::optional<raster::BBox> node_dirty_clip;
    u64 parallel_regions{0};
    u64 sequential_levels{0};
    bool scheduler_is_parallel{false};
};

bool same_optional_bbox(
    const std::optional<raster::BBox>& lhs,
    const std::optional<raster::BBox>& rhs)
{
    if (lhs.has_value() != rhs.has_value()) return false;
    if (!lhs) return true;
    return same_bbox(*lhs, *rhs);
}

void check_runtime_decision_parity(
    const RuntimeDiagnosticsObservation& off,
    const RuntimeDiagnosticsObservation& on)
{
    CHECK(off.dirty_rect_enabled == on.dirty_rect_enabled);
    CHECK(off.tile_execution_used == on.tile_execution_used);
    CHECK(off.fast_path_reused == on.fast_path_reused);
    CHECK(off.graph_reused == on.graph_reused);
    CHECK(off.layer_count == on.layer_count);
    CHECK(same_optional_bbox(off.dirty_rect, on.dirty_rect));
    CHECK(same_bbox(off.node_bbox, on.node_bbox));
    CHECK(same_optional_bbox(off.node_dirty_clip, on.node_dirty_clip));
    CHECK(off.parallel_regions == on.parallel_regions);
    CHECK(off.sequential_levels == on.sequential_levels);
    CHECK(off.pixel_hash == on.pixel_hash);
}

DiagnosticsParityObservation observe_runtime_node_diagnostics(
    SoftwareRenderer& renderer,
    bool multi_source,
    bool diagnostics_enabled,
    bool camera_2_5d)
{
    auto* resource = std::pmr::get_default_resource();
    if (multi_source) {
        const RenderNode first = RenderNodeFactory::rect(resource, "runtime_multi_a", {
            .size = {140.0f, 90.0f},
            .color = Color::red(),
            .pos = {260.0f, 70.0f, 0.0f},
        });
        const RenderNode second = RenderNodeFactory::rect(resource, "runtime_multi_b", {
            .size = {80.0f, 100.0f},
            .color = Color::blue(),
            .pos = {40.0f, 120.0f, 0.0f},
        });
        return observe_multi_source_diagnostics(
            renderer, first, second, diagnostics_enabled, camera_2_5d);
    }

    const RenderNode source = RenderNodeFactory::rect(resource, "runtime_source", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {280.0f, 100.0f, 0.0f},
    });
    return observe_source_diagnostics(
        renderer, source, diagnostics_enabled, camera_2_5d);
}

Scene make_runtime_parity_scene(bool multi_source, bool camera_2_5d) {
    SceneBuilder builder;
    builder.layer("runtime_parity_a", [multi_source, camera_2_5d](LayerBuilder& layer) {
        if (camera_2_5d) layer.enable_3d(true);
        layer.rect("runtime_a_red", {
            .size = {72.0f, 64.0f},
            .color = Color::red(),
            .pos = {48.0f, 44.0f, 0.0f},
        });
        if (multi_source) {
            layer.rect("runtime_a_green", {
                .size = {36.0f, 42.0f},
                .color = Color::green(),
                .pos = {138.0f, 80.0f, 0.0f},
            });
        }
    });
    builder.layer("runtime_parity_b", [multi_source, camera_2_5d](LayerBuilder& layer) {
        if (camera_2_5d) layer.enable_3d(true);
        layer.rect("runtime_b_blue", {
            .size = {60.0f, 76.0f},
            .color = Color::blue(),
            .pos = {208.0f, 112.0f, 0.0f},
        });
        if (multi_source) {
            layer.rect("runtime_b_yellow", {
                .size = {32.0f, 48.0f},
                .color = Color::yellow(),
                .pos = {248.0f, 38.0f, 0.0f},
            });
        }
    });
    return builder.build();
}

RuntimeDiagnosticsObservation observe_runtime_diagnostics(
    bool multi_source,
    bool camera_2_5d,
    bool diagnostics_enabled,
    bool parallel)
{
    SchedulerEnvironment scheduler_environment(parallel);
    auto renderer = test::make_renderer();
    auto settings = renderer.render_settings();
    settings.diagnostics.enabled = diagnostics_enabled;
    renderer.set_settings(settings);

    const auto node_observation = observe_runtime_node_diagnostics(
        renderer, multi_source, diagnostics_enabled, camera_2_5d);

    const Scene scene = make_runtime_parity_scene(multi_source, camera_2_5d);
    std::shared_ptr<Framebuffer> framebuffer;
    if (camera_2_5d) {
        Camera2_5D camera;
        camera.enabled = true;
        camera.position = {0.0f, 0.0f, -800.0f};
        camera.zoom = 800.0f;
        framebuffer = renderer.render_scene(
            scene, std::optional<Camera2_5D>{camera}, 320, 240, 30.0f);
    } else {
        framebuffer = renderer.render_scene(scene, Camera{}, 320, 240, 30.0f);
    }
    REQUIRE(framebuffer != nullptr);

    const auto* counters = renderer.counters();
    REQUIRE(counters != nullptr);
    return RuntimeDiagnosticsObservation{
        .pixel_hash = test::framebuffer_hash(*framebuffer),
        .dirty_rect_enabled = renderer.last_dirty_rect_enabled(),
        .tile_execution_used = renderer.last_tile_execution_used(),
        .fast_path_reused = renderer.last_fast_path_reused(),
        .graph_reused = renderer.last_graph_reused(),
        .layer_count = renderer.last_layer_count(),
        .dirty_rect = renderer.last_dirty_rect(),
        .node_bbox = node_observation.bbox,
        .node_dirty_clip = node_observation.dirty_clip,
        .parallel_regions = counters->parallel_regions_count.load(std::memory_order_relaxed),
        .sequential_levels = counters->level_sequential_count.load(std::memory_order_relaxed),
        .scheduler_is_parallel = renderer.scheduler().mode() == SchedulerMode::TbbFixed &&
                                 renderer.scheduler().concurrency() > 1,
    };
}

void check_scheduler_mode(const RuntimeDiagnosticsObservation& observation, bool parallel) {
    CHECK(observation.scheduler_is_parallel == parallel);
    if (parallel) {
        CHECK(observation.parallel_regions > 0);
    } else {
        CHECK(observation.parallel_regions == 0);
        CHECK(observation.sequential_levels > 0);
    }
}

void check_serial_parallel_parity(
    const RuntimeDiagnosticsObservation& serial,
    const RuntimeDiagnosticsObservation& parallel)
{
    CHECK(serial.pixel_hash == parallel.pixel_hash);
    CHECK(serial.dirty_rect_enabled == parallel.dirty_rect_enabled);
    CHECK(serial.tile_execution_used == parallel.tile_execution_used);
    CHECK(serial.fast_path_reused == parallel.fast_path_reused);
    CHECK(serial.graph_reused == parallel.graph_reused);
    CHECK(serial.layer_count == parallel.layer_count);
    CHECK(same_optional_bbox(serial.dirty_rect, parallel.dirty_rect));
    CHECK(same_bbox(serial.node_bbox, parallel.node_bbox));
    CHECK(same_optional_bbox(serial.node_dirty_clip, parallel.node_dirty_clip));
}

} // namespace

#define CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(name, multi_source, camera_2_5d) \
    TEST_CASE(name) { \
        const auto serial_off = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, false, false); \
        const auto serial_on = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, true, false); \
        const auto parallel_off = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, false, true); \
        const auto parallel_on = observe_runtime_diagnostics( \
            multi_source, camera_2_5d, true, true); \
        check_runtime_decision_parity(serial_off, serial_on); \
        check_runtime_decision_parity(parallel_off, parallel_on); \
        check_scheduler_mode(serial_off, false); \
        check_scheduler_mode(serial_on, false); \
        check_scheduler_mode(parallel_off, true); \
        check_scheduler_mode(parallel_on, true); \
        check_serial_parallel_parity(serial_off, parallel_off); \
        check_serial_parallel_parity(serial_on, parallel_on); \
    }

CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: SourceNode 2D serial and parallel", false, false)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: MultiSourceNode 2D serial and parallel", true, false)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: SourceNode Camera2_5D serial and parallel", false, true)
CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST(
    "Diagnostics runtime parity: MultiSourceNode Camera2_5D serial and parallel", true, true)

#undef CHRONON3D_DIAGNOSTICS_RUNTIME_PARITY_TEST

TEST_CASE("Diagnostics parity: SourceNode 2D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "source_2d_parity", {
        .size = {120.0f, 80.0f},
        .color = Color::red(),
        .pos = {280.0f, 100.0f, 0.0f},
    });

    const auto off = observe_source_diagnostics(renderer, source, false, false);
    const auto on = observe_source_diagnostics(renderer, source, true, false);
    check_parity_decision_and_pixels(off, on);
    CHECK(off.bbox.x1 == 320);
    CHECK(off.bbox.y1 <= 240);
}

TEST_CASE("Diagnostics parity: MultiSourceNode 2D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode first = RenderNodeFactory::rect(resource, "multi_2d_a", {
        .size = {140.0f, 90.0f},
        .color = Color::red(),
        .pos = {260.0f, 70.0f, 0.0f},
    });
    const RenderNode second = RenderNodeFactory::rect(resource, "multi_2d_b", {
        .size = {80.0f, 100.0f},
        .color = Color::blue(),
        .pos = {40.0f, 120.0f, 0.0f},
    });

    const auto off = observe_multi_source_diagnostics(renderer, first, second, false, false);
    const auto on = observe_multi_source_diagnostics(renderer, first, second, true, false);
    check_parity_decision_and_pixels(off, on);
    CHECK(off.bbox.x0 >= 0);
    CHECK(off.bbox.y0 >= 0);
    CHECK(off.bbox.x1 <= 320);
    CHECK(off.bbox.y1 <= 240);
}

TEST_CASE("Diagnostics parity: SourceNode Camera2_5D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode source = RenderNodeFactory::rect(resource, "source_camera_parity", {
        .size = {120.0f, 80.0f},
        .color = Color::green(),
        .pos = {40.0f, -30.0f, 0.0f},
    });

    const auto off = observe_source_diagnostics(renderer, source, false, true);
    const auto on = observe_source_diagnostics(renderer, source, true, true);
    check_parity_decision_and_pixels(off, on);
    CHECK(!off.bbox.is_empty());
}

TEST_CASE("Diagnostics parity: MultiSourceNode Camera2_5D keeps bbox decisions and pixels identical") {
    auto renderer = test::make_renderer();
    auto* resource = std::pmr::get_default_resource();
    const RenderNode first = RenderNodeFactory::rect(resource, "multi_camera_a", {
        .size = {100.0f, 70.0f},
        .color = Color::yellow(),
        .pos = {-50.0f, 20.0f, 0.0f},
    });
    const RenderNode second = RenderNodeFactory::rect(resource, "multi_camera_b", {
        .size = {70.0f, 110.0f},
        .color = Color::blue(),
        .pos = {80.0f, -30.0f, 0.0f},
    });

    const auto off = observe_multi_source_diagnostics(renderer, first, second, false, true);
    const auto on = observe_multi_source_diagnostics(renderer, first, second, true, true);
    check_parity_decision_and_pixels(off, on);
    CHECK(!off.bbox.is_empty());
}
