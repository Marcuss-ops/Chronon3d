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

} // namespace

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
    if (text == "desktop") { out = CpuMachineClass::Desktop; return true; }
    if (text == "laptop") { out = CpuMachineClass::Laptop; return true; }
    if (text == "server") { out = CpuMachineClass::Server; return true; }
    if (text == "embedded") { out = CpuMachineClass::Embedded; return true; }
    return false;
}

CpuBudget cpu_budget_for_class(CpuMachineClass c, int total_threads) noexcept {
    if (total_threads <= 0) total_threads = 1;

    int render = 0;
    int decode = 0;
    int encode = 0;

    switch (c) {
        case CpuMachineClass::Desktop:
            // 75% render, 12.5% decode, 12.5% encode
            // e.g. 16 cores -> 12/2/2
            render = total_threads * 3 / 4;
            decode = total_threads / 8;
            encode = total_threads / 8;
            break;
        case CpuMachineClass::Laptop:
            // Conservative: 50% render, 25% decode, 25% encode
            render = total_threads / 2;
            decode = total_threads / 4;
            encode = total_threads / 4;
            break;
        case CpuMachineClass::Server:
            // Render-heavy: 80% render, 10% decode, 10% encode
            render = total_threads * 4 / 5;
            decode = total_threads / 10;
            encode = total_threads / 10;
            break;
        case CpuMachineClass::Embedded:
            // Minimal: keep at least one thread per pool when possible
            render = total_threads - 2;
            decode = 1;
            encode = 1;
            break;
    }

    // Ensure at least one render thread.
    if (render < 1) render = 1;

    // Ensure at least one decode/encode thread when there is room.
    if (decode < 1 && total_threads >= 3) decode = 1;
    if (encode < 1 && total_threads >= 3) encode = 1;

    // Clamp to total_threads, prioritizing render, then decode, then encode.
    if (render + decode + encode > total_threads) {
        int remaining = total_threads;
        render = std::min(render, remaining);
        remaining -= render;
        decode = std::min(decode, remaining);
        remaining -= decode;
        encode = remaining;
    }

    if (render < 1) render = 1;

    return CpuBudget{render, decode, encode};
}

CpuBudget cpu_budget_from_environment(int total_threads) {
    if (total_threads <= 0) total_threads = 1;

    CpuBudget budget;

    const char* class_env = std::getenv("CHRONON3D_CPU_MACHINE_CLASS");
    CpuMachineClass cls = CpuMachineClass::Desktop;
    if (class_env && *class_env) {
        if (!parse_cpu_machine_class(class_env, cls)) {
            spdlog::warn("Invalid CHRONON3D_CPU_MACHINE_CLASS='{}'; defaulting to desktop", class_env);
            cls = CpuMachineClass::Desktop;
        }
    }

    budget = cpu_budget_for_class(cls, total_threads);

    // Explicit per-pool overrides take precedence.
    budget.render_threads = parse_positive_int_env("CHRONON3D_CPU_RENDER_THREADS", budget.render_threads);
    budget.decode_threads = parse_positive_int_env("CHRONON3D_CPU_DECODE_THREADS", budget.decode_threads);
    budget.encode_threads = parse_positive_int_env("CHRONON3D_CPU_ENCODE_THREADS", budget.encode_threads);

    // Clamp to the available hardware so the budget is never larger than
    // the total core count.
    budget.render_threads = std::min(budget.render_threads, total_threads);
    budget.decode_threads = std::min(budget.decode_threads, total_threads);
    budget.encode_threads = std::min(budget.encode_threads, total_threads);

    // Ensure the sum never exceeds the available hardware.  Render is
    // prioritised, then decode, then encode.
    if (budget.render_threads + budget.decode_threads + budget.encode_threads > total_threads) {
        int remaining = total_threads;
        budget.render_threads = std::min(budget.render_threads, remaining);
        remaining -= budget.render_threads;
        budget.decode_threads = std::min(budget.decode_threads, remaining);
        remaining -= budget.decode_threads;
        budget.encode_threads = remaining;
    }

    // Ensure at least one render thread.
    if (budget.render_threads < 1) budget.render_threads = 1;

    return budget;
}

} // namespace chronon3d
