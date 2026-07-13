// ── tests/core/test_cpu_budget.cpp ────────────────────────────────────
//
// Validates `chronon3d::CpuBudget` contract:
//   1. CpuBudget totals and empty() predicate.
//   2. Sums per class preset (matches existing user-spec verbatim: 16/Desktop = 12/2/2).
//   3. Presets never exceed total threads.
//   4. Machine class parsing.
//   5. Env vars override preset per-pool.
//   6. Invalid env vars fall back to defaults.
//   7. CHRONON3D_SCHEDULER_WORKERS overrides render threads.
//   8. (NEW F4.2) `total_threads` field equals factory input.
//   9. (NEW F4.2) `*_max_inflight` fields are class-conditional.
//
// F4.2 (TICKET-CPU-BUDGET-UNIFIED-V1) refactor: C++20 designated
// initializers replace positional aggregate inits because the struct
// order is now `[total_threads, render_threads, decode_threads,
// encode_threads, *_max_inflight, mode]` per user spec verbatim
// `int total_threads` first.

#include <chronon3d/core/cpu_budget.hpp>
#include <chronon3d/core/config.hpp>

#include <cstdlib>
#include <string>

#include <doctest/doctest.h>

using chronon3d::BudgetMode;
using chronon3d::CpuBudget;
using chronon3d::cpu_budget_for_class;
using chronon3d::cpu_budget_from_environment;
using chronon3d::parse_cpu_machine_class;
using chronon3d::cpu_machine_class_name;
using chronon3d::parse_cpu_budget_mode;
using chronon3d::cpu_budget_mode_name;

TEST_CASE("CpuBudget: totals and empty predicate") {
    // F4.2: designated initializer (positional `{12, 2, 2}` would now
    // ambiguously map to total_threads/render/decode since total_threads
    // is the 1st field per user spec).
    CpuBudget b{
        .total_threads = 16,
        .render_threads = 12,
        .decode_threads = 2,
        .encode_threads = 2,
    };
    CHECK(b.total_threads == 16);
    CHECK(b.total() == 16);
    CHECK_FALSE(b.empty());

    CpuBudget empty{
        .total_threads = 0,
        .render_threads = 0,
        .decode_threads = 0,
        .encode_threads = 0,
    };
    CHECK(empty.total() == 0);
    CHECK(empty.empty());
}

TEST_CASE("CpuBudget: desktop preset yields 12/2/2 on 16 threads") {
    CpuBudget b = cpu_budget_for_class(chronon3d::CpuMachineClass::Desktop, 16);
    CHECK(b.total_threads == 16);
    CHECK(b.render_threads == 12);
    CHECK(b.decode_threads == 2);
    CHECK(b.encode_threads == 2);
    CHECK(b.total() == 16);
}

TEST_CASE("CpuBudget: presets never exceed total threads") {
    for (auto cls : {chronon3d::CpuMachineClass::Desktop,
                     chronon3d::CpuMachineClass::Laptop,
                     chronon3d::CpuMachineClass::Server,
                     chronon3d::CpuMachineClass::Embedded}) {
        for (int total : {1, 2, 4, 8, 16, 32, 64, 128}) {
            CpuBudget b = cpu_budget_for_class(cls, total);
            CHECK(b.total_threads == total);
            CHECK(b.render_threads >= 1);
            CHECK(b.render_threads + b.decode_threads + b.encode_threads <= b.total_threads);
            CHECK(b.decode_threads >= 0);
            CHECK(b.encode_threads >= 0);
        }
    }
}

TEST_CASE("CpuBudget: machine class parsing") {
    using chronon3d::CpuMachineClass;
    CpuMachineClass cls{};
    CHECK(parse_cpu_machine_class("desktop", cls));
    CHECK(cls == CpuMachineClass::Desktop);
    CHECK(parse_cpu_machine_class("laptop", cls));
    CHECK(cls == CpuMachineClass::Laptop);
    CHECK(parse_cpu_machine_class("server", cls));
    CHECK(cls == CpuMachineClass::Server);
    CHECK(parse_cpu_machine_class("embedded", cls));
    CHECK(cls == CpuMachineClass::Embedded);
    CHECK_FALSE(parse_cpu_machine_class("unknown", cls));
    CHECK(cpu_machine_class_name(CpuMachineClass::Desktop) == "desktop");
}

TEST_CASE("CpuBudget: env vars override preset") {
    const std::string old_render = std::getenv("CHRONON3D_CPU_RENDER_THREADS") ? std::getenv("CHRONON3D_CPU_RENDER_THREADS") : "";
    const std::string old_decode = std::getenv("CHRONON3D_CPU_DECODE_THREADS") ? std::getenv("CHRONON3D_CPU_DECODE_THREADS") : "";
    const std::string old_encode = std::getenv("CHRONON3D_CPU_ENCODE_THREADS") ? std::getenv("CHRONON3D_CPU_ENCODE_THREADS") : "";
    const std::string old_mode = std::getenv("CHRONON3D_CPU_BUDGET_MODE") ? std::getenv("CHRONON3D_CPU_BUDGET_MODE") : "";

    struct EnvGuard {
        const std::string& old_value;
        const char* var_name;
        ~EnvGuard() {
            if (!old_value.empty()) ::setenv(var_name, old_value.c_str(), 1);
            else ::unsetenv(var_name);
        }
    };

    {
        EnvGuard g_r{old_render, "CHRONON3D_CPU_RENDER_THREADS"};
        EnvGuard g_d{old_decode, "CHRONON3D_CPU_DECODE_THREADS"};
        EnvGuard g_e{old_encode, "CHRONON3D_CPU_ENCODE_THREADS"};
        EnvGuard g_m{old_mode, "CHRONON3D_CPU_BUDGET_MODE"};
        ::setenv("CHRONON3D_CPU_RENDER_THREADS", "8", 1);
        ::setenv("CHRONON3D_CPU_DECODE_THREADS", "4", 1);
        ::setenv("CHRONON3D_CPU_ENCODE_THREADS", "4", 1);
        ::setenv("CHRONON3D_CPU_BUDGET_MODE", "dynamic", 1);

        CpuBudget b = cpu_budget_from_environment(16);
        CHECK(b.total_threads == 16);
        CHECK(b.render_threads == 8);
        CHECK(b.decode_threads == 4);
        CHECK(b.encode_threads == 4);
        CHECK(b.mode == BudgetMode::Dynamic);
    }
}

TEST_CASE("CpuBudget: invalid env vars fall back to defaults") {
    const std::string old_render = std::getenv("CHRONON3D_CPU_RENDER_THREADS") ? std::getenv("CHRONON3D_CPU_RENDER_THREADS") : "";
    const std::string old_decode = std::getenv("CHRONON3D_CPU_DECODE_THREADS") ? std::getenv("CHRONON3D_CPU_DECODE_THREADS") : "";
    const std::string old_encode = std::getenv("CHRONON3D_CPU_ENCODE_THREADS") ? std::getenv("CHRONON3D_CPU_ENCODE_THREADS") : "";

    struct EnvGuard {
        const std::string& old_value;
        const char* var_name;
        ~EnvGuard() {
            if (!old_value.empty()) ::setenv(var_name, old_value.c_str(), 1);
            else ::unsetenv(var_name);
        }
    };

    {
        EnvGuard g_r{old_render, "CHRONON3D_CPU_RENDER_THREADS"};
        EnvGuard g_d{old_decode, "CHRONON3D_CPU_DECODE_THREADS"};
        EnvGuard g_e{old_encode, "CHRONON3D_CPU_ENCODE_THREADS"};
        ::setenv("CHRONON3D_CPU_RENDER_THREADS", "abc", 1);
        ::setenv("CHRONON3D_CPU_DECODE_THREADS", "-3", 1);
        ::setenv("CHRONON3D_CPU_ENCODE_THREADS", "xyz", 1);

        CpuBudget b = cpu_budget_from_environment(16);
        CHECK(b.total_threads == 16);
        // Per-pool should fall back to the Desktop preset (the default
        // class is Desktop when CHRONON3D_CPU_MACHINE_CLASS is unset):
        // render=12, decode=2, encode=2.
        CHECK(b.render_threads == 12);
        CHECK(b.decode_threads == 2);
        CHECK(b.encode_threads == 2);
    }
}

TEST_CASE("CpuBudget: CHRONON3D_SCHEDULER_WORKERS overrides render threads") {
    // Per src/core/config.cpp the Config() ctor reads CHRONON3D_SCHEDULER_WORKERS
    // and clamps `cpu_budget_.render_threads = std::min(n, total)`. The test
    // sets the env var, then constructs Config (which auto-reads it) and
    // asserts the override propagated into the CpuBudget member.
    const std::string old_sched = std::getenv("CHRONON3D_SCHEDULER_WORKERS")
                                      ? std::getenv("CHRONON3D_SCHEDULER_WORKERS")
                                      : "";
    struct EnvGuard {
        const std::string& old_value;
        const char* var_name;
        ~EnvGuard() {
            if (!old_value.empty()) ::setenv(var_name, old_value.c_str(), 1);
            else ::unsetenv(var_name);
        }
    };

    {
        EnvGuard g{old_sched, "CHRONON3D_SCHEDULER_WORKERS"};
        ::setenv("CHRONON3D_SCHEDULER_WORKERS", "7", 1);
        // CpuBudget is built once and injected into Config; the legacy
        // env override is now folded into cpu_budget_from_environment().
        auto budget = cpu_budget_from_environment(16);
        CHECK(budget.render_threads == 7);
        CHECK(budget.render_threads >= 1);
        CHECK(budget.render_threads <= budget.total_threads);
    }
}

// ── F4.2 NEW: total_threads field is canonical input ─────────────────
TEST_CASE("CpuBudget: total_threads field = factory input anchor") {
    for (int n : {1, 2, 4, 8, 16, 32, 64}) {
        CpuBudget b = cpu_budget_for_class(chronon3d::CpuMachineClass::Desktop, n);
        CHECK(b.total_threads == n);
        CHECK(b.total() == n);
    }
}

// ── F4.2 NEW: per-pool max_inflight invariants ──────────────────────
TEST_CASE("CpuBudget: per-pool max_inflight is class-conditional") {
    using chronon3d::CpuMachineClass;
    // Desktop on 16 threads.
    CpuBudget desk = cpu_budget_for_class(CpuMachineClass::Desktop, 16);
    CHECK(desk.render_max_inflight == 2);
    CHECK(desk.decode_max_inflight == 2);
    CHECK(desk.encode_max_inflight == 4);

    // Server on 32 threads.
    CpuBudget serv = cpu_budget_for_class(CpuMachineClass::Server, 32);
    CHECK(serv.render_max_inflight == 2);
    CHECK(serv.decode_max_inflight == 4);
    CHECK(serv.encode_max_inflight == 4);

    // Laptop on 8 threads.
    CpuBudget lap = cpu_budget_for_class(CpuMachineClass::Laptop, 8);
    CHECK(lap.render_max_inflight == 2);
    CHECK(lap.decode_max_inflight == 2);
    CHECK(lap.encode_max_inflight == 2);

    // Embedded on 4 threads.
    CpuBudget emb = cpu_budget_for_class(CpuMachineClass::Embedded, 4);
    CHECK(emb.render_max_inflight == 2);
    CHECK(emb.decode_max_inflight >= 1);
    CHECK(emb.encode_max_inflight >= 1);
}

// ── F4.2 NEW: BudgetMode env override parses + names roundtrip ──────
TEST_CASE("CpuBudget: BudgetMode parses + names roundtrip") {
    BudgetMode m{};
    CHECK(parse_cpu_budget_mode("static", m));
    CHECK(m == BudgetMode::Static);
    CHECK(cpu_budget_mode_name(m) == "static");
    CHECK(parse_cpu_budget_mode("dynamic", m));
    CHECK(m == BudgetMode::Dynamic);
    CHECK(cpu_budget_mode_name(m) == "dynamic");
    CHECK_FALSE(parse_cpu_budget_mode("unknown", m));
}

TEST_CASE("CpuBudget: low-core overrides (3 and 4 threads)") {
    auto b3 = cpu_budget_for_class(CpuMachineClass::Desktop, 3);
    CHECK(b3.render_threads == 1);
    CHECK(b3.decode_threads == 1);
    CHECK(b3.encode_threads == 1);

    auto b4 = cpu_budget_for_class(CpuMachineClass::Desktop, 4);
    CHECK(b4.render_threads == 2);
    CHECK(b4.decode_threads == 1);
    CHECK(b4.encode_threads == 1);
}
