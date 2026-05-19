#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>
#include <chronon3d/runtime/telemetry/jsonl_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
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
