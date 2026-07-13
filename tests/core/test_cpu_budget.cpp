// ============================================================================
// tests/core/test_cpu_budget.cpp
//
// Unit tests for the unified CPU budget (render/decode/encode).
//
// Verifies:
//   1. CpuBudget totals and empty() predicate.
//   2. Per-machine-class presets sum to the requested core count.
//   3. Explicit env vars override presets.
//   4. Budget never exceeds hardware thread count.
// ============================================================================

#include <doctest/doctest.h>

#include <chronon3d/core/config.hpp>
#include <chronon3d/core/cpu_budget.hpp>
#include <cstdlib>
#include <string>

using chronon3d::CpuBudget;
using chronon3d::CpuMachineClass;
using chronon3d::cpu_budget_for_class;
using chronon3d::cpu_budget_from_environment;
using chronon3d::parse_cpu_machine_class;
using chronon3d::cpu_machine_class_name;

TEST_CASE("CpuBudget: totals and empty predicate") {
    CpuBudget b{12, 2, 2};
    CHECK(b.total() == 16);
    CHECK_FALSE(b.empty());

    CpuBudget empty{0, 0, 0};
    CHECK(empty.total() == 0);
    CHECK(empty.empty());
}

TEST_CASE("CpuBudget: desktop preset yields 12/2/2 on 16 threads") {
    CpuBudget b = cpu_budget_for_class(CpuMachineClass::Desktop, 16);
    CHECK(b.render_threads == 12);
    CHECK(b.decode_threads == 2);
    CHECK(b.encode_threads == 2);
    CHECK(b.total() == 16);
}

TEST_CASE("CpuBudget: presets never exceed total threads") {
    for (auto cls : {CpuMachineClass::Desktop, CpuMachineClass::Laptop,
                     CpuMachineClass::Server, CpuMachineClass::Embedded}) {
        for (int total : {1, 2, 4, 8, 16, 32, 64}) {
            CpuBudget b = cpu_budget_for_class(cls, total);
            CHECK(b.total() <= total);
            CHECK(b.render_threads >= 1);
            // decode/encode may be 0 when the total is too small to fit them.
            CHECK(b.decode_threads >= 0);
            CHECK(b.encode_threads >= 0);
        }
    }
}

TEST_CASE("CpuBudget: machine class parsing") {
    CpuMachineClass cls;
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
    // Save and restore env vars to avoid side effects.
    const std::string old_render = std::getenv("CHRONON3D_CPU_RENDER_THREADS") ? std::getenv("CHRONON3D_CPU_RENDER_THREADS") : "";
    const std::string old_decode = std::getenv("CHRONON3D_CPU_DECODE_THREADS") ? std::getenv("CHRONON3D_CPU_DECODE_THREADS") : "";
    const std::string old_encode = std::getenv("CHRONON3D_CPU_ENCODE_THREADS") ? std::getenv("CHRONON3D_CPU_ENCODE_THREADS") : "";
    const std::string old_class = std::getenv("CHRONON3D_CPU_MACHINE_CLASS") ? std::getenv("CHRONON3D_CPU_MACHINE_CLASS") : "";

    auto restore = [&]() {
        if (!old_render.empty()) ::setenv("CHRONON3D_CPU_RENDER_THREADS", old_render.c_str(), 1);
        else ::unsetenv("CHRONON3D_CPU_RENDER_THREADS");
        if (!old_decode.empty()) ::setenv("CHRONON3D_CPU_DECODE_THREADS", old_decode.c_str(), 1);
        else ::unsetenv("CHRONON3D_CPU_DECODE_THREADS");
        if (!old_encode.empty()) ::setenv("CHRONON3D_CPU_ENCODE_THREADS", old_encode.c_str(), 1);
        else ::unsetenv("CHRONON3D_CPU_ENCODE_THREADS");
        if (!old_class.empty()) ::setenv("CHRONON3D_CPU_MACHINE_CLASS", old_class.c_str(), 1);
        else ::unsetenv("CHRONON3D_CPU_MACHINE_CLASS");
    };

    ::setenv("CHRONON3D_CPU_RENDER_THREADS", "8", 1);
    ::setenv("CHRONON3D_CPU_DECODE_THREADS", "4", 1);
    ::setenv("CHRONON3D_CPU_ENCODE_THREADS", "4", 1);
    ::setenv("CHRONON3D_CPU_MACHINE_CLASS", "server", 1);

    {
        CpuBudget b = cpu_budget_from_environment(16);
        CHECK(b.render_threads == 8);
        CHECK(b.decode_threads == 4);
        CHECK(b.encode_threads == 4);
        CHECK(b.total() == 16);
    }

    restore();
}

TEST_CASE("CpuBudget: invalid env vars fall back to defaults") {
    const std::string old_render = std::getenv("CHRONON3D_CPU_RENDER_THREADS") ? std::getenv("CHRONON3D_CPU_RENDER_THREADS") : "";

    auto restore = [&]() {
        if (!old_render.empty()) ::setenv("CHRONON3D_CPU_RENDER_THREADS", old_render.c_str(), 1);
        else ::unsetenv("CHRONON3D_CPU_RENDER_THREADS");
    };

    ::setenv("CHRONON3D_CPU_RENDER_THREADS", "not-a-number", 1);

    {
        CpuBudget b = cpu_budget_from_environment(16);
        CHECK(b.render_threads >= 1);
        CHECK(b.total() <= 16);
    }

    restore();
}

TEST_CASE("CpuBudget: CHRONON3D_SCHEDULER_WORKERS overrides render threads") {
    const std::string old_workers = std::getenv("CHRONON3D_SCHEDULER_WORKERS") ? std::getenv("CHRONON3D_SCHEDULER_WORKERS") : "";

    auto restore = [&]() {
        if (!old_workers.empty()) ::setenv("CHRONON3D_SCHEDULER_WORKERS", old_workers.c_str(), 1);
        else ::unsetenv("CHRONON3D_SCHEDULER_WORKERS");
    };

    ::setenv("CHRONON3D_SCHEDULER_WORKERS", "7", 1);

    {
        chronon3d::Config cfg;
        CHECK(cfg.cpu_budget().render_threads == 7);
    }

    restore();
}
