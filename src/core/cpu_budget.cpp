#include <chronon3d/core/cpu_budget.hpp>

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <charconv>
#include <string_view>
#include <spdlog/spdlog.h>

namespace chronon3d {

namespace {

[[nodiscard]] int parse_positive_int_env(const char* env_name, int fallback) {
    const char* v = std::getenv(env_name);
    if (!v || !*v) return fallback;

    std::string_view sv(v);
    int value = 0;
    const auto* begin = sv.data();
    const auto* end = sv.data() + sv.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end || value < 0) {
        spdlog::warn("Invalid environment value {}='{}'; using default {}", env_name, v, fallback);
        return fallback;
    }
    return value;
}

[[nodiscard]] int clamp_min1(int value) noexcept {
    return std::max(1, value);
}

/// Class-conditional render_max_inflight preset.
[[nodiscard]] int render_inflight_for(CpuMachineClass c) noexcept {
    // Render is typically 2-frame double-buffered across all classes.
    (void)c;
    return 2;
}

/// Class-conditional decode_max_inflight preset.
[[nodiscard]] int decode_inflight_for(CpuMachineClass c) noexcept {
    switch (c) {
        case CpuMachineClass::Server:   return 4;
        case CpuMachineClass::Desktop:  return 2;
        case CpuMachineClass::Laptop:   return 2;
        case CpuMachineClass::Embedded: return 1;
    }
    return 2;
}

/// Class-conditional encode_max_inflight preset.
[[nodiscard]] int encode_inflight_for(CpuMachineClass c) noexcept {
    switch (c) {
        case CpuMachineClass::Server:   return 4;
        case CpuMachineClass::Desktop:  return 4;
        case CpuMachineClass::Laptop:   return 2;
        case CpuMachineClass::Embedded: return 1;
    }
    return 2;
}

} // namespace

std::string_view cpu_budget_mode_name(BudgetMode m) noexcept {
    switch (m) {
        case BudgetMode::Static:  return "static";
        case BudgetMode::Dynamic: return "dynamic";
    }
    return "static";
}

bool parse_cpu_budget_mode(std::string_view text, BudgetMode& out) noexcept {
    if (text == "static")  { out = BudgetMode::Static;  return true; }
    if (text == "dynamic") { out = BudgetMode::Dynamic; return true; }
    return false;
}

std::string_view cpu_machine_class_name(CpuMachineClass c) noexcept {
    switch (c) {
        case CpuMachineClass::Desktop:  return "desktop";
        case CpuMachineClass::Laptop:   return "laptop";
        case CpuMachineClass::Server:   return "server";
        case CpuMachineClass::Embedded: return "embedded";
    }
    return "desktop";
}

bool parse_cpu_machine_class(std::string_view text, CpuMachineClass& out) noexcept {
    if (text == "desktop")  { out = CpuMachineClass::Desktop;  return true; }
    if (text == "laptop")   { out = CpuMachineClass::Laptop;   return true; }
    if (text == "server")   { out = CpuMachineClass::Server;   return true; }
    if (text == "embedded") { out = CpuMachineClass::Embedded; return true; }
    return false;
}

CpuBudget cpu_budget_for_class(CpuMachineClass c, int total_threads) noexcept {
    CpuBudget b{};
    b.total_threads = (total_threads <= 0) ? 1 : total_threads;
    // `b.mode` defaults to BudgetMode::Static (struct inline-initializer);
    // we DO NOT re-assign here (Cat-3 no dead-store).
    b.render_max_inflight = render_inflight_for(c);
    b.decode_max_inflight = decode_inflight_for(c);
    b.encode_max_inflight = encode_inflight_for(c);

    int render = 0;
    int decode = 0;
    int encode = 0;

    switch (c) {
        case CpuMachineClass::Desktop:
            // 75% render, 12.5% decode, 12.5% encode (16 → 12/2/2)
            render = b.total_threads * 3 / 4;
            decode = b.total_threads / 8;
            encode = b.total_threads / 8;
            break;
        case CpuMachineClass::Laptop:
            // 50% render, 25% decode, 25% encode
            render = b.total_threads / 2;
            decode = b.total_threads / 4;
            encode = b.total_threads / 4;
            break;
        case CpuMachineClass::Server:
            // Render-heavy: 80% render, 10% decode, 10% encode
            render = b.total_threads * 4 / 5;
            decode = b.total_threads / 10;
            encode = b.total_threads / 10;
            break;
        case CpuMachineClass::Embedded:
            // Minimal: keep at least one thread per pool when possible
            render = b.total_threads - 2;
            decode = 1;
            encode = 1;
            break;
    }

    if (render < 1) render = 1;
    if (decode < 1 && b.total_threads >= 3) decode = 1;
    if (encode < 1 && b.total_threads >= 3) encode = 1;

    if (render + decode + encode > b.total_threads) {
        int remaining = b.total_threads;
        render = std::min(render, remaining);
        remaining -= render;
        decode = std::min(decode, remaining);
        remaining -= decode;
        encode = remaining;
    }

    if (render < 1) render = 1;

    // Low-core override: keep at least one thread per pool whenever possible.
    if (total_threads == 1) {
        render = 1;
        decode = 0;
        encode = 0;
    } else if (total_threads == 2) {
        render = 1;
        decode = 0;
        encode = 1;
    } else if (total_threads == 3) {
        render = 1;
        decode = 1;
        encode = 1;
    } else if (total_threads == 4) {
        render = 2;
        decode = 1;
        encode = 1;
    }

    b.render_threads = render;
    b.decode_threads = decode;
    b.encode_threads = encode;
    return b;
}

CpuBudget cpu_budget_from_environment(int total_threads) {
    if (total_threads <= 0) total_threads = 1;

    const char* class_env = std::getenv("CHRONON3D_CPU_MACHINE_CLASS");
    CpuMachineClass cls = CpuMachineClass::Desktop;
    if (class_env && *class_env) {
        if (!parse_cpu_machine_class(class_env, cls)) {
            spdlog::warn("Invalid CHRONON3D_CPU_MACHINE_CLASS='{}'; defaulting to desktop", class_env);
            cls = CpuMachineClass::Desktop;
        }
    }

    CpuBudget budget = cpu_budget_for_class(cls, total_threads);

    // Mode override (defaults to Static from the factory).
    const char* mode_env = std::getenv("CHRONON3D_CPU_BUDGET_MODE");
    if (mode_env && *mode_env) {
        if (!parse_cpu_budget_mode(mode_env, budget.mode)) {
            spdlog::warn("Invalid CHRONON3D_CPU_BUDGET_MODE='{}'; defaulting to static", mode_env);
            budget.mode = BudgetMode::Static;
        }
    }

    // Legacy override: CHRONON3D_SCHEDULER_WORKERS sets the render pool.
    // It is folded into the unified budget so the same immutable instance
    // is used by TBB, scheduler, decoder and encoder.
    {
        const char* workers_env = std::getenv("CHRONON3D_SCHEDULER_WORKERS");
        if (workers_env && *workers_env) {
            std::string_view sv(workers_env);
            int n = 0;
            const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
            if (ec == std::errc{} && ptr == sv.data() + sv.size() && n > 0) {
                budget.render_threads = std::min(n, total_threads);
            } else {
                spdlog::warn(
                    "Invalid CHRONON3D_SCHEDULER_WORKERS='{}'; using CpuBudget render threads",
                    workers_env);
            }
        }
    }

    // Explicit per-pool overrides take precedence over the preset.
    budget.render_threads = parse_positive_int_env("CHRONON3D_CPU_RENDER_THREADS", budget.render_threads);
    budget.decode_threads = parse_positive_int_env("CHRONON3D_CPU_DECODE_THREADS", budget.decode_threads);
    budget.encode_threads = parse_positive_int_env("CHRONON3D_CPU_ENCODE_THREADS", budget.encode_threads);

    // Clamp per-pool to total_threads so the budget is never larger than
    // the available hardware.
    budget.render_threads = std::min(budget.render_threads, budget.total_threads);
    budget.decode_threads = std::min(budget.decode_threads, budget.total_threads);
    budget.encode_threads = std::min(budget.encode_threads, budget.total_threads);

    // Ensure the sum never exceeds the available hardware.  Render is
    // prioritised, then decode, then encode.
    if (budget.render_threads + budget.decode_threads + budget.encode_threads > budget.total_threads) {
        int remaining = budget.total_threads;
        budget.render_threads = std::min(budget.render_threads, remaining);
        remaining -= budget.render_threads;
        budget.decode_threads = std::min(budget.decode_threads, remaining);
        remaining -= budget.decode_threads;
        budget.encode_threads = remaining;
    }

    if (budget.render_threads < 1) budget.render_threads = 1;

    return budget;
}

} // namespace chronon3d
