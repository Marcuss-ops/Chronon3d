// test_telemetry_policy.cpp — Split from test_telemetry.cpp: telemetry
// level gating (Summary vs Detailed vs Off), transaction commit/rollback
// semantics and the NullTelemetryStore (telemetry OFF) surface.
#include <doctest/doctest.h>
#include <chronon3d/runtime/telemetry/telemetry_manager.hpp>

#include <chronon3d/runtime/telemetry/sqlite_telemetry_store.hpp>
#include <chronon3d/runtime/telemetry/null_telemetry_store.hpp>
#include <chronon3d/core/telemetry/render_telemetry.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <memory>
#include <vector>
using namespace chronon3d;

using namespace chronon3d::telemetry;

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

class TxTrackingStore final : public TelemetryStore {
public:
    bool fail_counters{false};
    bool last_commit{true};
    int begin_count{0};

    bool initialize(const std::string&) override { return true; }
    bool write_render_run(const RenderTelemetryRecord&) override { return true; }
    bool write_frames(const std::string&, const std::vector<FrameTelemetry>&) override { return true; }
    bool write_phases(const std::string&, const std::vector<PhaseTelemetryRecord>&) override { return true; }
    bool write_counters(const std::string&, const std::vector<CounterTelemetryRecord>&) override {
        return !fail_counters;
    }
    bool write_node_events(const std::string&, const std::vector<NodeTelemetryRecord>&) override { return true; }
    bool write_layer_events(const std::string&, const std::vector<LayerTelemetryRecord>&) override { return true; }
    bool write_cache_events(const std::string&, const std::vector<CacheTelemetryRecord>&) override { return true; }
    bool write_culling_events(const std::string&, const std::vector<CullingTelemetryRecord>&) override { return true; }
    bool write_image_events(const std::string&, const std::vector<ImageTelemetryRecord>&) override { return true; }
    bool write_artifacts(const std::string&, const std::vector<RenderArtifactRecord>&) override { return true; }

    void begin_transaction() override { ++begin_count; }
    void end_transaction(bool commit) override { last_commit = commit; }
};

TEST_CASE("Telemetry: Summary level persists durable rows only (granular gated)") {
    auto mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager manager;  // default level = Summary
    manager.add_store(mock);

    TelemetryRunSnapshot snapshot;
    snapshot.run.run_id = "summary-run";
    snapshot.run.success = true;
    snapshot.frames.push_back({.frame_number = 0, .duration_ms = 16.6, .cache_hit = true, .dirty_area_ratio = 1.0});
    snapshot.phases.push_back({"render", 10.0});
    snapshot.counters.push_back({"cache_hits", 100});
    snapshot.node_events.push_back({.node_name = "n", .duration_ms = 1.0});
    snapshot.layer_events.push_back({.layer_id = "l"});
    snapshot.image_events.push_back({.image_path = "i.png"});

    REQUIRE(manager.record_run(snapshot));
    CHECK(mock->run_written);
    CHECK(mock->phases_written);
    CHECK(mock->counters_written);
    // Granular per-frame/per-event tables must NOT be written at Summary.
    CHECK_FALSE(mock->frames_written);
    CHECK_FALSE(mock->node_events_written);
    CHECK_FALSE(mock->layer_events_written);
    CHECK_FALSE(mock->image_events_written);

    // Same snapshot content at Detailed reaches the granular tables.
    auto detailed_mock = std::make_shared<MockTelemetryStore>();
    TelemetryManager detailed_manager;
    detailed_manager.set_level(TelemetryLevel::Detailed);
    detailed_manager.add_store(detailed_mock);
    REQUIRE(detailed_manager.record_run(snapshot));
    CHECK(detailed_mock->frames_written);
    CHECK(detailed_mock->node_events_written);
    CHECK(detailed_mock->layer_events_written);
    CHECK(detailed_mock->image_events_written);
}

TEST_CASE("Telemetry: failed write rolls back the transaction (end_transaction(false))") {
    auto store = std::make_shared<TxTrackingStore>();
    TelemetryManager manager;
    manager.set_level(TelemetryLevel::Detailed);
    manager.add_store(store);

    TelemetryRunSnapshot snapshot;
    snapshot.run.run_id = "tx-run";
    snapshot.run.success = true;
    snapshot.counters.push_back({"cache_hits", 1});
    snapshot.frames.push_back({.frame_number = 0, .duration_ms = 16.6, .cache_hit = true, .dirty_area_ratio = 0.5});

    // Success path: commit is requested.
    REQUIRE(manager.record_run(snapshot));
    CHECK(store->begin_count == 1);
    CHECK(store->last_commit);

    // Failure path: counters write fails mid-transaction → rollback requested.
    store->fail_counters = true;
    store->last_commit = true;
    TelemetryRunSnapshot failing;
    failing.run.run_id = "tx-run-fail";
    failing.run.success = false;
    failing.counters.push_back({"cache_hits", 1});
    CHECK_FALSE(manager.record_run(failing));
    CHECK(store->begin_count == 2);
    CHECK_FALSE(store->last_commit);
}

TEST_CASE("Telemetry: Off level records nothing") {
    auto store = std::make_shared<TxTrackingStore>();
    TelemetryManager manager;
    manager.set_level(TelemetryLevel::Off);
    manager.add_store(store);

    TelemetryRunSnapshot snapshot;
    snapshot.run.run_id = "off-run";
    snapshot.run.success = true;
    snapshot.counters.push_back({"cache_hits", 1});

    CHECK(manager.record_run(snapshot));
    CHECK(store->begin_count == 0);
    CHECK(store->last_commit);  // untouched: no transaction was opened
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
    auto null_store = std::make_shared<NullTelemetryStore>();
    // The immutable TelemetryRunSnapshot is never mutated by record_run:
    // run metadata defaults (run_id/os/cpu) are filled on the manager's own
    // copy and become observable only through the store.  Register a
    // capturing store next to the null one to assert that contract while
    // keeping the null-store robustness point.
    auto capture = std::make_shared<MockTelemetryStore>();
    manager.add_store(null_store);
    manager.add_store(capture);

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
    CHECK(ok);
    CHECK(capture->run_written);
    // run_id should be auto-generated by the manager before store write
    CHECK(!capture->last_run.run_id.empty());
    // os/cpu should be auto-populated by the manager before store write
    CHECK(!capture->last_run.os.empty());
    CHECK(!capture->last_run.cpu_model.empty());
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
