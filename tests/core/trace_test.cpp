#include <doctest/doctest.h>
#include <chronon3d/core/profiling/trace.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <set>
#include <vector>
#include <fstream>
#include <filesystem>

using namespace chronon3d;

// ============================================================================
// RenderTrace Unit Tests
// ============================================================================

TEST_CASE("TraceScope registers duration") {
    RenderTrace trace;

    {
        TraceScope s(&trace, "test", "unit", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto events = trace.events();
    REQUIRE(events.size() == 1);
    const auto& e = events[0];
    REQUIRE(e.name == "test");
    REQUIRE(e.category == "unit");
    REQUIRE(e.frame == 0);
    REQUIRE(e.dur_us >= 1500); // At least 1.5ms
    REQUIRE(e.ts_us > 0);
    REQUIRE(e.thread_hash > 0);
}

TEST_CASE("disabled trace does not allocate events") {
    RenderTrace trace;
    trace.set_enabled(false);

    {
        TraceScope s(&trace, "skip", "unit", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(trace.events().empty());
}

TEST_CASE("enabled then disabled mid-scope") {
    RenderTrace trace;

    {
        TraceScope s1(&trace, "first", "test", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    trace.set_enabled(false);

    {
        TraceScope s2(&trace, "second", "test", 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(trace.events().size() == 1);
    REQUIRE(trace.events()[0].name == "first");
}

TEST_CASE("nested scopes maintain correct ordering") {
    RenderTrace trace;

    {
        TraceScope outer(&trace, "outer", "frame", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        {
            TraceScope inner(&trace, "inner", "node", 1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    const auto events = trace.events();
    REQUIRE(events.size() == 2);

    // Destructor order: inner destroyed first (events[0]), outer destroyed second (events[1]).
    // Inner started later than outer, so events[0].ts_us >= events[1].ts_us.
    CHECK(events[0].ts_us >= events[1].ts_us);

    // Names should be present regardless of order
    bool has_outer = false, has_inner = false;
    for (const auto& e : events) {
        if (e.name == "outer") has_outer = true;
        if (e.name == "inner") has_inner = true;
    }
    CHECK(has_outer);
    CHECK(has_inner);
}

TEST_CASE("thread safety — concurrent recording from multiple threads") {
    RenderTrace trace;

    auto work = [&] {
        for (int i = 0; i < 100; ++i) {
            TraceScope s(&trace, "t", "parallel", 0);
        }
    };

    std::thread a(work), b(work), c(work), d(work);
    a.join(); b.join(); c.join(); d.join();

    REQUIRE(trace.events().size() == 400);

    // Each thread should have its own thread_hash
    std::set<uint32_t> ids;
    for (const auto& e : trace.events()) {
        ids.insert(e.thread_hash);
    }
    REQUIRE(ids.size() >= 4);
}

TEST_CASE("events bounded under heavy concurrent load") {
    RenderTrace trace;

    auto worker = [&](int count) {
        for (int i = 0; i < count; ++i) {
            TraceScope s(&trace, "burst", "test", i % 10);
        }
    };

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(worker, 250);
    }
    for (auto& t : threads) t.join();

    CHECK(trace.events().size() == 2000);
}

TEST_CASE("TraceScope nullptr trace is safe") {
    TraceScope s(nullptr, "safe", "test", 0);
    // Should not crash
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

TEST_CASE("clear resets events") {
    RenderTrace trace;

    {
        TraceScope s(&trace, "a", "test", 0);
    }
    CHECK(trace.events().size() == 1);

    trace.clear();
    CHECK(trace.events().empty());

    // After clear, new events can still be recorded
    {
        TraceScope s(&trace, "b", "test", 0);
    }
    CHECK(trace.events().size() == 1);
}

TEST_CASE("JSON output is valid Chrome trace format") {
    RenderTrace trace;

    {
        TraceScope s(&trace, "blur", "effect", 42);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        TraceScope s(&trace, "render", "frame", 42);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const auto tmp_path = std::filesystem::temp_directory_path() / "_chronon_trace_test.json";
    write_trace_json(trace, tmp_path.string());

    // Parse and validate
    auto j = nlohmann::json::parse(std::ifstream(tmp_path.string()));

    REQUIRE(j.contains("traceEvents"));
    REQUIRE(j["traceEvents"].is_array());
    REQUIRE(j["traceEvents"].size() == 2);

    // Chrome trace format checks
    CHECK(j["traceEvents"][0]["ph"] == "X"); // "X" = Complete event
    CHECK(j["traceEvents"][0]["name"] == "blur");
    CHECK(j["traceEvents"][0]["cat"] == "effect");
    CHECK(j["traceEvents"][0].contains("ts"));
    CHECK(j["traceEvents"][0].contains("dur"));
    CHECK(j["traceEvents"][0].contains("tid"));
    CHECK(j["traceEvents"][0]["pid"] == 1);

    // args should contain frame
    REQUIRE(j["traceEvents"][0].contains("args"));
    REQUIRE(j["traceEvents"][0]["args"]["frame"] == 42);

    // Second event
    CHECK(j["traceEvents"][1]["name"] == "render");
    CHECK(j["traceEvents"][1]["cat"] == "frame");

    // Clean up
    std::filesystem::remove(tmp_path);
}
