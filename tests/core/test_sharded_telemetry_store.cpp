#include <doctest/doctest.h>


#include <chronon3d/core/telemetry/render_telemetry.hpp>

#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <vector>
using namespace chronon3d;

namespace {

// A simple record type with a deterministic string id and a sequence
// number.  The ShardedTelemetryStore is a template so we can instantiate
// it with this local type without depending on the project-internal
// telemetry record types.
struct TestRecord {
    std::string id;
    int seq{0};
    bool operator==(const TestRecord& o) const { return id == o.id && seq == o.seq; }
};

using Store = chronon3d::telemetry::detail::ShardedTelemetryStore<TestRecord>;

} // namespace

TEST_CASE("ShardedTelemetryStore: collect() returns every recorded record (no loss, no dup)") {
    Store store;
    constexpr int kCount = 1000;
    for (int i = 0; i < kCount; ++i) {
        store.record(TestRecord{"rec-" + std::to_string(i), i});
    }

    auto collected = store.collect();
    CHECK(collected.size() == static_cast<std::size_t>(kCount));

    // Every unique id must be present exactly once.
    std::set<std::string> seen_ids;
    for (const auto& r : collected) {
        CHECK(seen_ids.insert(r.id).second); // no duplicates
    }
    CHECK(seen_ids.size() == static_cast<std::size_t>(kCount));

    // And every seq number 0..kCount-1 must be present.
    std::set<int> seen_seqs;
    for (const auto& r : collected) {
        seen_seqs.insert(r.seq);
    }
    CHECK(seen_seqs.size() == static_cast<std::size_t>(kCount));
}

TEST_CASE("ShardedTelemetryStore: order within a single shard is preserved") {
    // Round-robin sharding distributes records across 16 shards.  The
    // collect() method returns records grouped by shard (shard 0 first,
    // then shard 1, …), so the global order is NOT the insertion order.
    // However, within each shard the order IS the insertion order.
    //
    // This test verifies the intra-shard ordering by counting how many
    // records from each insertion-window land in each shard and checking
    // that those records appear in monotonically increasing seq order.
    Store store;
    constexpr int kCount = 256; // 256 / 16 = 16 records per shard
    for (int i = 0; i < kCount; ++i) {
        store.record(TestRecord{"r-" + std::to_string(i), i});
    }

    auto collected = store.collect();
    REQUIRE(collected.size() == static_cast<std::size_t>(kCount));

    // Group the collected records by shard index, then check that each
    // group's seq numbers are strictly increasing.
    constexpr std::size_t kShards = Store::kShards;
    std::vector<std::vector<int>> per_shard_seqs(kShards);
    for (std::size_t i = 0; i < collected.size(); ++i) {
        const std::size_t shard = i % kShards; // round-robin assignment at record time
        per_shard_seqs[shard].push_back(collected[i].seq);
    }
    for (std::size_t s = 0; s < kShards; ++s) {
        const auto& seqs = per_shard_seqs[s];
        for (std::size_t j = 1; j < seqs.size(); ++j) {
            CAPTURE(s);
            CAPTURE(j);
            CHECK(seqs[j] > seqs[j - 1]); // strictly increasing within a shard
        }
    }
}

TEST_CASE("ShardedTelemetryStore: clear() empties all shards") {
    Store store;
    for (int i = 0; i < 50; ++i) {
        store.record(TestRecord{"x-" + std::to_string(i), i});
    }
    CHECK(store.collect().size() == 50);

    store.clear();
    CHECK(store.collect().size() == 0);
    // Calling clear() again is a no-op.
    store.clear();
    CHECK(store.collect().size() == 0);
}

TEST_CASE("ShardedTelemetryStore: concurrent recorders from N threads do not lose records") {
    // 8 threads, each recording 500 unique records.  After joining, the
    // total collected count must equal 4000 and every (thread_id, i)
    // pair must appear exactly once.
    Store store;
    constexpr int kThreads = 8;
    constexpr int kPerThread = 500;

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kPerThread; ++i) {
                store.record(TestRecord{
                    "t" + std::to_string(t) + "-i" + std::to_string(i),
                    t * kPerThread + i,
                });
            }
        });
    }
    for (auto& t : threads) t.join();

    auto collected = store.collect();
    CHECK(collected.size() == static_cast<std::size_t>(kThreads * kPerThread));

    std::set<std::string> seen_ids;
    for (const auto& r : collected) {
        CHECK(seen_ids.insert(r.id).second);
    }
    CHECK(seen_ids.size() == static_cast<std::size_t>(kThreads * kPerThread));
}
