// tests/core/test_concurrency_budget.cpp
// ════════════════════════════════════════════════════════════════════════════
// Certifies the single concurrency budget invariant:
//
//   CpuBudget → ExecutionScheduler arena → tbb::global_control
//   ─────────────────────────────────────────────────────
//   ┌─────────────────────────────────────────────────┐
//   │  Single authority on parallelism                │
//   │  CpuBudget (render/decode/encode split)          │
//   │       ↓                                          │
//   │  tbb::global_control(max_allowed_parallelism,    │
//   │      render_threads)                             │
//   │       ↓                                          │
//   │  ExecutionScheduler::task_arena(slots =          │
//   │      render_threads)                             │
//   │       ↓                                          │
//   │  All tbb::parallel_for / for_each_tile calls     │
//   │  go through the arena, capped by global_control  │
//   └─────────────────────────────────────────────────┘
//
// No oversubscription: N frame threads DO NOT create N*T TBB workers
// because frames are rendered sequentially (single frame thread).
// All internal parallelism uses the same capped TBB pool.
// ════════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/config.hpp>
#include <chronon3d/core/scheduler/execution_scheduler.hpp>

#include <doctest/doctest.h>

#include <thread>

namespace cs = chronon3d;

TEST_CASE("CpuBudget: render+decode+encode sum does not exceed total") {
    for (int total : {1, 2, 3, 4, 8, 16, 32, 64, 128}) {
        auto budget = cs::cpu_budget_for_class(cs::CpuMachineClass::Desktop, total);
        const int sum = budget.render_threads + budget.decode_threads + budget.encode_threads;
        CHECK_MESSAGE(sum <= total,
            "total=" << total << " render=" << budget.render_threads
            << " decode=" << budget.decode_threads
            << " encode=" << budget.encode_threads
            << " sum=" << sum);
        // Every pool must have at least one thread when total >= 3 (per contract).
        if (total >= 3) {
            CHECK(budget.render_threads >= 1);
            CHECK(budget.decode_threads >= 1);
            CHECK(budget.encode_threads >= 1);
        }
        // Render pool must be the largest pool.
        CHECK(budget.render_threads >= budget.decode_threads);
        CHECK(budget.render_threads >= budget.encode_threads);
    }
}

TEST_CASE("CpuBudget: Laptop class split is conservative") {
    auto budget = cs::cpu_budget_for_class(cs::CpuMachineClass::Laptop, 8);
    // Laptop: 50% render, 25% decode, 25% encode → 4/2/2
    CHECK_EQ(budget.render_threads, 4);
    CHECK_EQ(budget.decode_threads, 2);
    CHECK_EQ(budget.encode_threads, 2);
}

TEST_CASE("CpuBudget: Server class is render-heavy") {
    auto budget = cs::cpu_budget_for_class(cs::CpuMachineClass::Server, 16);
    // Server: 80% render, 10% decode, 10% encode → 12/1/1 (min 1 each)
    CHECK_EQ(budget.render_threads, 12);
    CHECK(budget.decode_threads >= 1);
    CHECK(budget.encode_threads >= 1);
}

TEST_CASE("CpuBudget: Embedded class is minimal") {
    auto budget = cs::cpu_budget_for_class(cs::CpuMachineClass::Embedded, 4);
    // Embedded: render = total - 2 = 2, decode = 1, encode = 1
    CHECK_EQ(budget.render_threads, 2);
    CHECK_EQ(budget.decode_threads, 1);
    CHECK_EQ(budget.encode_threads, 1);
}

TEST_CASE("ExecutionScheduler: arena concurrency matches CpuBudget") {
    // Verify that the ExecutionScheduler arena size is consistent with
    // the CpuBudget render_threads when constructed via make_execution_scheduler(Config&).
    auto config = cs::Config{};
    // Set a known CpuBudget.
    const cs::CpuBudget test_budget{
        .total_threads = 8,
        .render_threads = 6,
        .decode_threads = 1,
        .encode_threads = 1,
    };
    config.set_cpu_budget(test_budget);

    auto scheduler = cs::make_execution_scheduler(config);
    // The arena concurrency should be bounded by render_threads.
    CHECK(scheduler.concurrency() <= 6);
    CHECK(scheduler.worker_count() <= 6);
}

TEST_CASE("ExecutionScheduler: Sequential mode forces arena(1)") {
    auto scheduler = cs::ExecutionScheduler{cs::SchedulerMode::Sequential, 0, false};
    CHECK_EQ(scheduler.concurrency(), 1);
    CHECK_EQ(scheduler.worker_count(), 1);
    CHECK_EQ(scheduler.mode(), cs::SchedulerMode::Sequential);
}

TEST_CASE("ExecutionScheduler: TbbFixed mode respects worker_count") {
    auto scheduler = cs::ExecutionScheduler{cs::SchedulerMode::TbbFixed, 4, false};
    CHECK(scheduler.concurrency() <= 4);
    CHECK(scheduler.concurrency() >= 1);
}

TEST_CASE("CpuBudget: total() matches input") {
    for (int total : {1, 2, 4, 8, 16}) {
        auto budget = cs::cpu_budget_for_class(cs::CpuMachineClass::Desktop, total);
        CHECK_EQ(budget.total(), total);
    }
}

TEST_CASE("CpuBudget: render_threads never exceeds total") {
    for (int total : {1, 2, 3, 4, 8, 16, 32, 64}) {
        for (auto cls : {cs::CpuMachineClass::Desktop, cs::CpuMachineClass::Laptop,
                         cs::CpuMachineClass::Server, cs::CpuMachineClass::Embedded}) {
            auto budget = cs::cpu_budget_for_class(cls, total);
            CHECK(budget.render_threads <= budget.total());
            // render_threads must be >= 1 when total >= 1.
            if (total >= 1) {
                CHECK(budget.render_threads >= 1);
            }
            // The sum of all pools must not exceed the total.
            const int sum = budget.render_threads + budget.decode_threads + budget.encode_threads;
            CHECK(sum <= budget.total());
        }
    }
}