#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
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

    std::vector<NodeTelemetryRecord> last_node_events;
    std::vector<LayerTelemetryRecord> last_layer_events;

    bool initialize(const std::string&) override {
        initialized = true;
        return true;
    }
    bool write_render_run(const RenderTelemetryRecord&) override {
        run_written = true;
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
    bool write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>&) override { return true; }
    bool write_tile_events(const std::string&, const std::vector<TileTelemetryRecord>&) override { return true; }
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

TEST_CASE("Telemetry: JsonlTelemetryStore serialization") {
    auto temp_dir = std::filesystem::temp_directory_path() / "chronon3d_test_telemetry";
    std::filesystem::create_directories(temp_dir);
    auto jsonl_path = (temp_dir / "test_history.jsonl").string();

    // Clean any prior file
    std::filesystem::remove(jsonl_path);

    auto store = std::make_shared<JsonlTelemetryStore>();
    REQUIRE(store->initialize(jsonl_path));

    RenderTelemetryRecord run;
    run.run_id = "run_123";
    run.composition_id = "comp_abc";
    run.success = true;
    run.frames_total = 5;
    run.effective_fps = 60.0;
    run.pixels_touched = 10000;
    run.dirty_full_fallbacks = 7;
    run.dirty_full_fallback_predicted_bounds_missing = 3;
    run.dirty_full_fallback_composite_missing_input_bounds = 2;
    run.dirty_full_fallback_transform_bounds_unknown = 1;
    run.dirty_full_fallback_effect_bounds_unknown = 1;

    REQUIRE(store->write_render_run(run));

    std::vector<FrameTelemetryRecord> frames = {
        {0, 15.0, true, 0.5},
        {1, 18.0, false, 1.0}
    };
    REQUIRE(store->write_frames("run_123", frames));

    // Verify written file content
    std::ifstream file(jsonl_path);
    REQUIRE(file.is_open());

    std::string line;
    int line_count = 0;
    while (std::getline(file, line)) {
        auto j = nlohmann::json::parse(line);
        if (line_count == 0) {
            CHECK(j["type"] == "run");
            CHECK(j["run_id"] == "run_123");
            CHECK(j["composition_id"] == "comp_abc");
            CHECK(j["success"] == true);
            CHECK(j["frames_total"] == 5);
            CHECK(j["effective_fps"] == 60.0);
            CHECK(j["pixels_touched"] == 10000);
            CHECK(j["dirty_full_fallbacks"] == 7);
            CHECK(j["dirty_full_fallback_predicted_bounds_missing"] == 3);
            CHECK(j["dirty_full_fallback_composite_missing_input_bounds"] == 2);
            CHECK(j["dirty_full_fallback_transform_bounds_unknown"] == 1);
            CHECK(j["dirty_full_fallback_effect_bounds_unknown"] == 1);
        } else if (line_count == 1) {
            CHECK(j["type"] == "frame");
            CHECK(j["run_id"] == "run_123");
            CHECK(j["frame_number"] == 0);
            CHECK(j["duration_ms"] == 15.0);
            CHECK(j["cache_hit"] == true);
            CHECK(j["dirty_area_ratio"] == 0.5);
        } else if (line_count == 2) {
            CHECK(j["type"] == "frame");
            CHECK(j["run_id"] == "run_123");
            CHECK(j["frame_number"] == 1);
            CHECK(j["duration_ms"] == 18.0);
            CHECK(j["cache_hit"] == false);
            CHECK(j["dirty_area_ratio"] == 1.0);
        }
        line_count++;
    }
    CHECK(line_count == 3);

    // Clean up
    file.close();
    std::filesystem::remove_all(temp_dir);
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

TEST_CASE("Telemetry: Jsonl node/layer event serialization") {
    auto temp_dir = std::filesystem::temp_directory_path() / "chronon3d_test_telemetry2";
    std::filesystem::create_directories(temp_dir);
    auto jsonl_path = (temp_dir / "test_node_layer.jsonl").string();
    std::filesystem::remove(jsonl_path);

    auto store = std::make_shared<JsonlTelemetryStore>();
    REQUIRE(store->initialize(jsonl_path));

    // Write node events
    std::vector<NodeTelemetryRecord> node_events;
    node_events.push_back({
        .run_id = "",
        .frame_number = 1,
        .node_name = "my_node",
        .node_type = "Transform",
        .duration_ms = 20.0,
        .cache_status = "miss",
        .input_count = 1,
        .output_width = 800,
        .output_height = 600,
        .output_bytes = 800 * 600 * 4,
    });
    REQUIRE(store->write_node_events("run_node_test", node_events));

    // Write layer events
    std::vector<LayerTelemetryRecord> layer_events;
    layer_events.push_back({
        .run_id = "",
        .frame_number = 1,
        .layer_id = "panel",
        .layer_name = "Panel",
        .layer_type = "RoundedRect",
        .duration_ms = 100.5,
        .visible = true,
        .opacity = 0.8f,
        .blend_mode = "Normal",
        .bbox_w = 400, .bbox_h = 300,
        .area_pixels = 120000,
        .visible_pixels = 120000,
        .effects = "blur,shadow",
    });
    REQUIRE(store->write_layer_events("run_layer_test", layer_events));

    // Verify JSONL content
    std::ifstream file(jsonl_path);
    REQUIRE(file.is_open());

    std::string line;
    std::getline(file, line);
    {
        auto j = nlohmann::json::parse(line);
        CHECK(j["type"] == "node_event");
        CHECK(j["run_id"] == "run_node_test");
        CHECK(j["frame_number"] == 1);
        CHECK(j["node_name"] == "my_node");
        CHECK(j["node_type"] == "Transform");
        CHECK(j["cache_status"] == "miss");
        CHECK(j["output_width"] == 800);
        CHECK(j["output_height"] == 600);
        CHECK(j["duration_ms"] == 20.0);
    }

    std::getline(file, line);
    {
        auto j = nlohmann::json::parse(line);
        CHECK(j["type"] == "layer_event");
        CHECK(j["run_id"] == "run_layer_test");
        CHECK(j["layer_id"] == "panel");
        CHECK(j["layer_name"] == "Panel");
        CHECK(j["layer_type"] == "RoundedRect");
        CHECK(j["duration_ms"] == 100.5);
        CHECK(j["visible"] == true);
        CHECK(j["opacity"].get<double>() == doctest::Approx(0.8));
        CHECK(j["effects"] == "blur,shadow");
    }

    file.close();
    std::filesystem::remove_all(temp_dir);
}
