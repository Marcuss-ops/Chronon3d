#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/null_telemetry_store.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
using namespace chronon3d;

using namespace chronon3d::telemetry;

TEST_CASE("Telemetry: System Info Queries") {
    std::string os = TelemetryManager::get_os_name();
    std::string cpu = TelemetryManager::get_cpu_model();
    int cores = TelemetryManager::get_logical_cores();
    std::string compiler = TelemetryManager::get_compiler_info();
    std::string build = TelemetryManager::get_build_type();

    CHECK(!os.empty());
    CHECK(!cpu.empty());
    CHECK(cores > 0);
    CHECK(!compiler.empty());
    CHECK(!build.empty());
}

class MockTelemetryStore : public TelemetryStore {
public:
    bool initialized{false};
    bool run_written{false};
    bool frames_written{false};
    bool phases_written{false};
    bool counters_written{false};
    bool node_events_written{false};
    bool layer_events_written{false};
    bool image_events_written{false};

    RenderTelemetryRecord last_run;
    std::vector<NodeTelemetryRecord> last_node_events;
    std::vector<LayerTelemetryRecord> last_layer_events;
    std::vector<ImageTelemetryRecord> last_image_events;

    bool initialize(const std::string&) override {
        initialized = true;
        return true;
    }
    bool write_render_run(const RenderTelemetryRecord& run) override {
        run_written = true;
        last_run = run;
        return true;
    }
    bool write_frames(const std::string&, const std::vector<FrameTelemetryRecord>&) override {
        frames_written = true;
        return true;
    }
    bool write_phases(const std::string&, const std::vector<PhaseTelemetryRecord>&) override {
        phases_written = true;
        return true;
    }
    bool write_counters(const std::string&, const std::vector<CounterTelemetryRecord>&) override {
        counters_written = true;
        return true;
    }
    bool write_node_events(const std::string&, const std::vector<NodeTelemetryRecord>& events) override {
        node_events_written = true;
        last_node_events = events;
        return true;
    }
    bool write_layer_events(const std::string&, const std::vector<LayerTelemetryRecord>& events) override {
        layer_events_written = true;
        last_layer_events = events;
        return true;
    }
    bool write_cache_events(const std::string&, const std::vector<CacheTelemetryRecord>&) override { return true; }
    bool write_culling_events(const std::string&, const std::vector<CullingTelemetryRecord>&) override { return true; }
    bool write_text_events(const std::string&, const std::vector<TextTelemetryRecord>&) override { return true; }
    bool write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>& events) override {
        image_events_written = true;
        last_image_events = events;
        return true;
    }
    bool write_tile_events(const std::string&, const std::vector<TileTelemetryRecord>&) override { return true; }
    bool write_artifacts(const std::string&, const std::vector<RenderArtifactRecord>&) override { return true; }
};

TEST_CASE("Telemetry: TelemetryManager and MockStore Orchestration") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;
    manager.add_store(mock);

    RenderTelemetryRecord run;
    run.composition_id = "test_comp";
    run.success = true;

    std::vector<FrameTelemetryRecord> frames = {{0, 16.6, true, 1.0}};
    std::vector<PhaseTelemetryRecord> phases = {{"render", 10.0}};
    std::vector<CounterTelemetryRecord> counters = {{"cache_hits", 100}};

    bool ok = manager.record_run(run, frames, phases, counters);
    CHECK(ok);
    CHECK(mock->run_written);
    CHECK(mock->frames_written);
    CHECK(mock->phases_written);
    CHECK(mock->counters_written);
}

TEST_CASE("Telemetry: node accumulator collects and clears") {
    // Must clear any events from previous tests
    collect_node_telemetry();
    collect_layer_telemetry();

    record_node_telemetry({.node_name="node_a", .duration_ms=1.0});
    record_node_telemetry({.node_name="node_b", .duration_ms=2.0});

    auto events = collect_node_telemetry();
    CHECK(events.size() == 2);
    CHECK(events[0].node_name == "node_a");
    CHECK(events[1].node_name == "node_b");

    // Buffer should be cleared after collect
    CHECK(collect_node_telemetry().empty());
}

TEST_CASE("Telemetry: MockStore node and layer events") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;
    manager.add_store(mock);

    RenderTelemetryRecord run;
    run.composition_id = "test_node_layer";
    run.success = true;

    std::vector<NodeTelemetryRecord> node_events;
    node_events.push_back({
        .run_id = "",
        .frame_number = 0,
        .node_name = "test_node_1",
        .node_type = "Source",
        .duration_ms = 12.5,
        .cache_status = "miss",
        .input_count = 0,
        .output_width = 640,
        .output_height = 480,
        .output_bytes = 640 * 480 * 4,
    });
    node_events.push_back({
        .run_id = "",
        .frame_number = 0,
        .node_name = "test_node_2",
        .node_type = "Composite",
        .duration_ms = 45.3,
        .cache_status = "hit",
        .input_count = 2,
        .output_width = 1920,
        .output_height = 1080,
        .output_bytes = 1920 * 1080 * 4,
    });

    std::vector<LayerTelemetryRecord> layer_events;
    layer_events.push_back({
        .run_id = "",
        .frame_number = 0,
        .layer_id = "bg",
        .layer_name = "Background",
        .layer_type = "Rect",
        .duration_ms = 58.3,
        .visible = true,
        .opacity = 1.0f,
        .blend_mode = "Normal",
        .bbox_w = 1920, .bbox_h = 1080,
        .area_pixels = 2073600,
        .visible_pixels = 2073600,
    });

    bool ok = manager.record_run(run, {}, {}, {}, node_events, layer_events);
    CHECK(ok);
    CHECK(mock->run_written);
    CHECK(mock->node_events_written);
    CHECK(mock->layer_events_written);
    CHECK(mock->last_node_events.size() == 2);
    CHECK(mock->last_node_events[0].node_name == "test_node_1");
    CHECK(mock->last_node_events[0].node_type == "Source");
    CHECK(mock->last_node_events[0].cache_status == "miss");
    CHECK(mock->last_node_events[0].output_bytes == 640 * 480 * 4);
    CHECK(mock->last_node_events[1].node_name == "test_node_2");
    CHECK(mock->last_node_events[1].node_type == "Composite");
    CHECK(mock->last_node_events[1].cache_status == "hit");
    CHECK(mock->last_layer_events.size() == 1);
    CHECK(mock->last_layer_events[0].layer_id == "bg");
    CHECK(mock->last_layer_events[0].area_pixels == 2073600);
}

TEST_CASE("Telemetry: image events are forwarded to the store") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;
    manager.add_store(mock);

    RenderTelemetryRecord run;
    run.composition_id = "test_image_events";
    run.success = true;

    std::vector<ImageTelemetryRecord> image_events;
    image_events.push_back({
        .run_id = "",
        .frame_number = 7,
        .layer_id = "image_layer",
        .image_path = "assets/test.png",
        .image_width = 800,
        .image_height = 600,
        .cache_status = "hit",
        .decode_ms = 1.5,
        .sample_ms = 3.25,
        .sampled_pixels = 480000,
    });

    bool ok = manager.record_run(run, {}, {}, {}, {}, {}, {}, {}, {}, image_events);
    CHECK(ok);
    CHECK(mock->run_written);
    CHECK(mock->image_events_written);
    CHECK(mock->last_image_events.size() == 1);
    CHECK(mock->last_image_events[0].cache_status == "hit");
    CHECK(mock->last_image_events[0].sampled_pixels == 480000);
}

TEST_CASE("Telemetry: image sampling metrics survive run storage") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;
    manager.add_store(mock);

    RenderTelemetryRecord run;
    run.composition_id = "test_image_metrics";
    run.success = true;
    run.image_decode_ms = 1.25;
    run.image_sample_ms = 3.5;
    run.image_sampled_pixels = 480000;

    bool ok = manager.record_run(run);
    CHECK(ok);
    CHECK(mock->run_written);
    CHECK(mock->last_run.composition_id == "test_image_metrics");
    CHECK(mock->last_run.image_decode_ms == doctest::Approx(1.25));
    CHECK(mock->last_run.image_sample_ms == doctest::Approx(3.5));
    CHECK(mock->last_run.image_sampled_pixels == 480000);
}

// ── NullTelemetryStore tests (telemetry OFF path) ────────────────────────

TEST_CASE("NullTelemetryStore: all writes succeed without SQLite") {
    NullTelemetryStore null_store;

    CHECK(null_store.initialize("any/path.db"));

    RenderTelemetryRecord run;
    run.composition_id = "null_test";
    CHECK(null_store.write_render_run(run));

    std::vector<FrameTelemetryRecord> frames = {{0, 16.6, true, 0.1}};
    CHECK(null_store.write_frames("run_1", frames));

    std::vector<PhaseTelemetryRecord> phases = {{"render", 10.0}};
    CHECK(null_store.write_phases("run_1", phases));

    std::vector<CounterTelemetryRecord> counters = {{"test", 42}};
    CHECK(null_store.write_counters("run_1", counters));

    std::vector<NodeTelemetryRecord> node_events;
    node_events.push_back({.node_name="n", .duration_ms=1.0});
    CHECK(null_store.write_node_events("run_1", node_events));

    std::vector<LayerTelemetryRecord> layer_events;
    layer_events.push_back({.layer_id="l", .layer_name="L"});
    CHECK(null_store.write_layer_events("run_1", layer_events));

    std::vector<CacheTelemetryRecord> cache_events;
    cache_events.push_back({.node_name="c"});
    CHECK(null_store.write_cache_events("run_1", cache_events));

    std::vector<CullingTelemetryRecord> culling_events;
    culling_events.push_back({.layer_id="cull"});
    CHECK(null_store.write_culling_events("run_1", culling_events));

    std::vector<TextTelemetryRecord> text_events;
    text_events.push_back({.font_path="f"});
    CHECK(null_store.write_text_events("run_1", text_events));

    std::vector<ImageTelemetryRecord> image_events;
    image_events.push_back({.image_path="img"});
    CHECK(null_store.write_image_events("run_1", image_events));

    std::vector<TileTelemetryRecord> tile_events;
    tile_events.push_back({.layer_id="t"});
    CHECK(null_store.write_tile_events("run_1", tile_events));

    // begin/end_transaction are no-ops (base class defaults)
    null_store.begin_transaction();
    null_store.end_transaction(true);
}

TEST_CASE("NullTelemetryStore: TelemetryManager with null store does not fail") {
    TelemetryManager manager;
    manager.add_store(std::make_shared<NullTelemetryStore>());

    RenderTelemetryRecord run;
    run.composition_id = "null_mgr";
    run.success = true;

    std::vector<FrameTelemetryRecord> frames = {{0, 10.0, true, 0.5}};
    std::vector<PhaseTelemetryRecord> phases = {{"setup", 5.0}};
    std::vector<CounterTelemetryRecord> counters = {{"cache_hits", 1}};

    bool ok = manager.record_run(run, frames, phases, counters);
    CHECK(ok);
    // run_id should be auto-generated
    CHECK(!run.run_id.empty());
    // os/cpu should be auto-populated
    CHECK(!run.os.empty());
    CHECK(!run.cpu_model.empty());
}

TEST_CASE("NullTelemetryStore: TelemetryManager record_run with empty stores succeeds") {
    TelemetryManager manager;
    manager.clear_stores();

    RenderTelemetryRecord run;
    run.composition_id = "empty_stores";
    run.success = false;

    // record_run should succeed even with zero stores registered
    bool ok = manager.record_run(run);
    CHECK(ok);
}
