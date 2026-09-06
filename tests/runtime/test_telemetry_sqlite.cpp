// test_telemetry_sqlite.cpp — Split from test_telemetry.cpp: SQLite
// schema-version stamping, node/memory summary persistence, snapshot
// transaction semantics, the retention janitor and legacy v0 top-up
// (all gated by CHRONON3D_ENABLE_SQLITE_TELEMETRY).
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
    m.framebuffer_copies.store(5);
    m.framebuffer_clears.store(6);
    tracker.observe_node("BlurNode42", m);
    // live_bytes/peak_live_bytes are point-in-time state that the reporter
    // folds exclusively via the acquire/release path (observe_live_bytes) —
    // observe_node() intentionally ignores them.  Drive the 1024-byte peak
    // the way production does: a temporary workspace acquire.
    tracker.acquire_temporary("BlurNode42", std::nullopt, 1024);
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
    // temporary_buffers: 2 from the metrics observation + 1 from the acquire
    CHECK(sqlite3_column_int64(stmt, 7) == 3);
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
    // current/peak live bytes reflect the 1024-byte temporary acquire
    CHECK(sqlite3_column_int64(stmt, 1) == 1024);
    CHECK(sqlite3_column_int64(stmt, 2) == 1024);
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

TEST_CASE("SQLite telemetry: retention janitor purges Detailed rows of old runs only") {
    const auto db_path = unique_telemetry_db("retention");
    std::filesystem::remove(db_path);
    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));

        RenderTelemetryRecord old_run;
        old_run.run_id = "old-run";
        old_run.success = true;
        old_run.finished_at_iso = "2020-01-01T00:00:00Z";
        REQUIRE(store.write_render_run(old_run));

        RenderTelemetryRecord fresh_run;
        fresh_run.run_id = "fresh-run";
        fresh_run.success = true;
        fresh_run.finished_at_iso = TelemetryManager::get_current_iso_time();
        REQUIRE(store.write_render_run(fresh_run));

        const std::vector<FrameTelemetry> frames = {
            {.frame_number = 0, .duration_ms = 16.6, .cache_hit = true, .dirty_area_ratio = 1.0}};
        REQUIRE(store.write_frames("old-run", frames));
        REQUIRE(store.write_frames("fresh-run", frames));
        const std::vector<NodeTelemetryRecord> node_events = {
            {.node_name = "old-node", .duration_ms = 1.0}};
        REQUIRE(store.write_node_events("old-run", node_events));

        // Janitor: 30-day window keeps the fresh run, drops the 2020 run's
        // granular rows, and never touches render_runs itself.
        store.apply_retention(30);
    }

    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);

    auto scalar_int = [db](const char* sql) {
        sqlite3_stmt* stmt = nullptr;
        REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
        const int value = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return value;
    };

    CHECK(scalar_int(
        "SELECT COUNT(*) FROM render_frames WHERE run_id = 'old-run';") == 0);
    CHECK(scalar_int(
        "SELECT COUNT(*) FROM render_frames WHERE run_id = 'fresh-run';") == 1);
    CHECK(scalar_int(
        "SELECT COUNT(*) FROM render_node_events WHERE run_id = 'old-run';") == 0);
    // Durable Summary rows are never purged.
    CHECK(scalar_int("SELECT COUNT(*) FROM render_runs;") == 2);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("SQLite telemetry: legacy v0 DB is column-topped-up and preserved") {
    const auto db_path = unique_telemetry_db("legacy-v0");
    std::filesystem::remove(db_path);

    // Create a pre-versioning database: minimal render_runs + one row.
    {
        sqlite3* raw = nullptr;
        REQUIRE(sqlite3_open(db_path.string().c_str(), &raw) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "CREATE TABLE render_runs (run_id TEXT PRIMARY KEY, composition_id TEXT, success INTEGER);",
            nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_exec(raw,
            "INSERT INTO render_runs (run_id, composition_id, success) VALUES ('legacy-1', 'legacy-comp', 1);",
            nullptr, nullptr, nullptr) == SQLITE_OK);
        REQUIRE(sqlite3_close(raw) == SQLITE_OK);
    }

    {
        SqliteTelemetryStore store;
        REQUIRE(store.initialize(db_path.string()));
        RenderTelemetryRecord run;
        run.run_id = "legacy-1";
        run.composition_id = "legacy-comp";
        run.success = true;
        run.frames_total = 3;
        REQUIRE(store.write_render_run(run));
    }

    CHECK(query_user_version(db_path) >= 1);
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(db_path.string().c_str(), &db) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db,
        "SELECT COUNT(*), composition_id, frames_total FROM render_runs WHERE run_id = 'legacy-1';",
        -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int(stmt, 0) == 1);
    CHECK(std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))) ==
          "legacy-comp");
    CHECK(sqlite3_column_int(stmt, 2) == 3);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
#endif
