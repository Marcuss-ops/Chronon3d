#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/null_telemetry_store.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>
#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <sqlite3.h>
#include <set>
#endif
using namespace chronon3d;

using namespace chronon3d::telemetry;

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
TEST_CASE("SQLite telemetry: initializes the canonical render_runs schema and inserts a run") {
    const auto db_path = std::filesystem::temp_directory_path() /
        ("chronon3d-telemetry-schema-" + std::to_string(
            static_cast<unsigned long long>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count())) + ".sqlite");
    std::filesystem::remove(db_path);

    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));

        RenderTelemetryRecord run;
        run.run_id = "schema-contract-run";
        run.composition_id = "schema-contract-composition";
        run.success = true;
        run.frames_total = 3;
        run.frames_written = 3;
        run.framebuffer_pool_empty_alloc = 7;
        run.framebuffer_pool_best_fit_reuse = 11;

        REQUIRE(store.write_render_run(run));
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.c_str(), &db) == SQLITE_OK);

    sqlite3_stmt* columns_stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(
        db, "PRAGMA table_info(render_runs);", -1, &columns_stmt, nullptr) == SQLITE_OK);
    std::set<std::string> columns;
    while (sqlite3_step(columns_stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(columns_stmt, 1));
        if (name != nullptr) columns.emplace(name);
    }
    sqlite3_finalize(columns_stmt);

    CHECK(columns.contains("framebuffer_pool_empty_alloc"));
    CHECK(columns.contains("framebuffer_pool_best_fit_reuse"));
    CHECK_FALSE(columns.contains("framebuffer_pool_miss_count_empty"));
    CHECK_FALSE(columns.contains("framebuffer_pool_miss_count_best_fit"));
    CHECK_FALSE(columns.contains("framebuffer_pool_miss_count_size_mismatch"));

    sqlite3_stmt* run_stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(
        db,
        "SELECT run_id, composition_id, frames_total, frames_written, "
        "framebuffer_pool_empty_alloc, framebuffer_pool_best_fit_reuse "
        "FROM render_runs WHERE run_id = ?1;",
        -1, &run_stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(
        run_stmt, 1, "schema-contract-run", -1, SQLITE_STATIC) == SQLITE_OK);
    REQUIRE(sqlite3_step(run_stmt) == SQLITE_ROW);
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(run_stmt, 0))) ==
          "schema-contract-run");
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(run_stmt, 1))) ==
          "schema-contract-composition");
    CHECK(sqlite3_column_int(run_stmt, 2) == 3);
    CHECK(sqlite3_column_int(run_stmt, 3) == 3);
    CHECK(sqlite3_column_int(run_stmt, 4) == 7);
    CHECK(sqlite3_column_int(run_stmt, 5) == 11);
    sqlite3_finalize(run_stmt);

    CHECK(sqlite3_close(db) == SQLITE_OK);
    CHECK(std::filesystem::remove(db_path));
}
#endif

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
    bool write_frames(const std::string&, const std::vector<FrameTelemetry>&) override {
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
    bool write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>& events) override {
        image_events_written = true;
        last_image_events = events;
        return true;
    }
    bool write_artifacts(const std::string&, const std::vector<RenderArtifactRecord>&) override { return true; }
};

TEST_CASE("Telemetry: TelemetryManager and MockStore Orchestration") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;
    manager.add_store(mock);

    RenderTelemetryRecord run;
    run.composition_id = "test_comp";
    run.success = true;

    std::vector<FrameTelemetry> frames = {
        {.frame_number = 0, .duration_ms = 16.6, .cache_hit = true, .dirty_area_ratio = 1.0}};
    std::vector<PhaseTelemetryRecord> phases = {{"render", 10.0}};
    std::vector<CounterTelemetryRecord> counters = {{"cache_hits", 100}};

    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    snapshot.frames = frames;
    snapshot.phases = phases;
    snapshot.counters = counters;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
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

    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    snapshot.node_events = node_events;
    snapshot.layer_events = layer_events;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
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

    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    snapshot.image_events = image_events;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
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
    run.image_decode_wall_ms = 1.25;
    run.image_sample_ms = 3.5;
    run.image_sampled_pixels = 480000;

    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
    CHECK(ok);
    CHECK(mock->run_written);
    CHECK(mock->last_run.composition_id == "test_image_metrics");
    CHECK(mock->last_run.image_decode_wall_ms == doctest::Approx(1.25));
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

    std::vector<FrameTelemetry> frames = {
        {.frame_number = 0, .duration_ms = 16.6, .cache_hit = true, .dirty_area_ratio = 0.1}};
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

    std::vector<ImageTelemetryRecord> image_events;
    image_events.push_back({.image_path="img"});
    CHECK(null_store.write_image_events("run_1", image_events));

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

    std::vector<FrameTelemetry> frames = {
        {.frame_number = 0, .duration_ms = 10.0, .cache_hit = true, .dirty_area_ratio = 0.5}};
    std::vector<PhaseTelemetryRecord> phases = {{"setup", 5.0}};
    std::vector<CounterTelemetryRecord> counters = {{"cache_hits", 1}};

    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    snapshot.frames = frames;
    snapshot.phases = phases;
    snapshot.counters = counters;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
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
    TelemetryRunSnapshot snapshot;
    snapshot.run = run;
    bool ok = manager.record_run(snapshot);
    run = snapshot.run;  // legacy back-fill of manager-filled defaults
    CHECK(ok);
}

#ifdef CHRONON3D_ENABLE_SQLITE_TELEMETRY
#include <chronon3d/runtime/telemetry/telemetry_run_snapshot.hpp>
#include <chronon3d/internal/render_graph/node_memory_tracker.hpp>

namespace {

std::filesystem::path unique_telemetry_db(const char* tag) {
    return std::filesystem::temp_directory_path() /
        (std::string("chronon3d-") + tag + "-" + std::to_string(
            static_cast<unsigned long long>(std::chrono::high_resolution_clock::now()
                .time_since_epoch().count())) + ".sqlite");
}

int query_user_version(const std::filesystem::path& db_path) {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    int version = -1;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK);
    if (sqlite3_step(stmt) == SQLITE_ROW) version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return version;
}

} // namespace

TEST_CASE("SQLite telemetry: fresh DB is stamped with the current schema version") {
    const auto db_path = unique_telemetry_db("schema-version");
    std::filesystem::remove(db_path);
    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));
    }
    CHECK(query_user_version(db_path) >= 1);
    std::filesystem::remove(db_path);
}

TEST_CASE("SQLite telemetry: re-initializing an existing DB is idempotent and preserves data") {
    const auto db_path = unique_telemetry_db("reinit");
    std::filesystem::remove(db_path);
    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));
        RenderTelemetryRecord run;
        run.run_id = "reinit-run";
        run.success = true;
        REQUIRE(store.write_render_run(run));
    }
    // Second open of the SAME file: versioned path must not duplicate or fail.
    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));
    }
    CHECK(query_user_version(db_path) >= 1);

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM render_runs;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int(stmt, 0) == 1);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("SQLite telemetry: NodeMemoryTracker snapshot persists exactly into summary tables") {
    using chronon3d::graph::NodeMemoryTracker;
    using chronon3d::graph::NodeMemoryMetrics;

    NodeMemoryTracker tracker;
    NodeMemoryMetrics m;
    m.pixels_read.store(100);
    m.pixels_written.store(200);
    m.bytes_read.store(1600);
    m.bytes_written.store(3200);
    m.allocations.store(3);
    m.allocated_bytes.store(4096);
    m.temporary_buffers.store(2);
    m.peak_live_bytes.store(1024);
    m.framebuffer_copies.store(5);
    m.framebuffer_clears.store(6);
    tracker.observe_node("BlurNode42", m);
    tracker.record_rss_peak(1u << 30);

    const auto report = tracker.snapshot();
    REQUIRE(report.nodes.size() == 1);

    const auto db_path = unique_telemetry_db("node-summary");
    std::filesystem::remove(db_path);
    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));

        // Build the projection the same way the CLI finalize does.
        std::vector<NodeSummaryTelemetryRecord> summaries;
        for (const auto& node : report.nodes) {
            auto& s = summaries.emplace_back();
            s.node_id = node.node_id;
            s.pixels_read = node.pixels_read;
            s.pixels_written = node.pixels_written;
            s.bytes_read = node.bytes_read;
            s.bytes_written = node.bytes_written;
            s.allocations = node.allocations;
            s.allocated_bytes = node.allocated_bytes;
            s.temporary_buffers = node.temporary_buffers;
            s.peak_live_bytes = node.peak_live_bytes;
            s.framebuffer_copies = node.framebuffer_copies;
            s.framebuffer_clears = node.framebuffer_clears;
        }
        MemorySummaryTelemetryRecord mem;
        mem.peak_rss_bytes = report.peak_rss_bytes;
        mem.current_live_bytes = report.current_live_bytes;
        mem.peak_live_bytes = report.peak_live_bytes;

        store.begin_transaction();
        const bool ok = store.write_node_summaries("parity-run", summaries) &&
                        store.write_memory_summary("parity-run", mem);
        store.end_transaction(ok);
        REQUIRE(ok);
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
        "SELECT node_id, pixels_read, pixels_written, bytes_read, bytes_written, "
        "allocations, allocated_bytes, temporary_buffers, peak_live_bytes, "
        "framebuffer_copies, framebuffer_clears "
        "FROM render_node_summary WHERE run_id = ?1;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(stmt, 1, "parity-run", -1, SQLITE_STATIC) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) == "BlurNode42");
    CHECK(sqlite3_column_int64(stmt, 1) == 100);
    CHECK(sqlite3_column_int64(stmt, 2) == 200);
    CHECK(sqlite3_column_int64(stmt, 3) == 1600);
    CHECK(sqlite3_column_int64(stmt, 4) == 3200);
    CHECK(sqlite3_column_int64(stmt, 5) == 3);
    CHECK(sqlite3_column_int64(stmt, 6) == 4096);
    CHECK(sqlite3_column_int64(stmt, 7) == 2);
    CHECK(sqlite3_column_int64(stmt, 8) == 1024);
    CHECK(sqlite3_column_int64(stmt, 9) == 5);
    CHECK(sqlite3_column_int64(stmt, 10) == 6);
    sqlite3_finalize(stmt);

    REQUIRE(sqlite3_prepare_v2(db,
        "SELECT peak_rss_bytes, current_live_bytes, peak_live_bytes "
        "FROM render_memory_summary WHERE run_id = ?1;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(stmt, 1, "parity-run", -1, SQLITE_STATIC) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int64(stmt, 0) == (1u << 30));
    CHECK(sqlite3_column_int64(stmt, 1) == 0);
    CHECK(sqlite3_column_int64(stmt, 2) == 0);
    sqlite3_finalize(stmt);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("TelemetryRunSnapshot: record_run(snapshot) writes summaries inside one transaction") {
    const auto db_path = unique_telemetry_db("snapshot");
    std::filesystem::remove(db_path);

    TelemetryManager manager;
    auto store = std::make_shared<SqliteTelemetryStore>();
    REQUIRE(store->initialize(db_path.string()));
    manager.clear_stores();
    manager.add_store(store);

    TelemetryRunSnapshot snapshot;
    snapshot.run.run_id = "snapshot-run";
    snapshot.run.success = true;
    snapshot.phases.push_back({"setup", 5.0});
    snapshot.counters.push_back({"cache_hits", 42});
    auto& s = snapshot.node_summaries.emplace_back();
    s.node_id = "Text8";
    s.calls = 10;
    s.total_ms = 3.5;
    s.avg_ms = 0.35;
    s.cache_hits = 8;
    s.cache_misses = 2;
    s.output_bytes = 8192;
    snapshot.memory_summary.peak_live_bytes = 2048;

    REQUIRE(manager.record_run(snapshot));

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
        "SELECT calls, total_ms, cache_hits, output_bytes FROM render_node_summary WHERE run_id = ?1;",
        -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_bind_text(stmt, 1, "snapshot-run", -1, SQLITE_STATIC) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int64(stmt, 0) == 10);
    CHECK(sqlite3_column_double(stmt, 1) == doctest::Approx(3.5));
    CHECK(sqlite3_column_int64(stmt, 2) == 8);
    CHECK(sqlite3_column_int64(stmt, 3) == 8192);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
#endif
