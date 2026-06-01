// ── Micro-benchmark: std::any_cast chain vs std::variant dispatch ─────────
// Measures the raw dispatch overhead of extracting effect parameters using
// the OLD pattern (std::any_cast chain with runtime type_info comparison)
// vs the NEW pattern (std::get_if via std::variant with O(1) index dispatch).
//
// The benchmark uses the actual project effect types (BlurParams, TintParams,
// GlowParams, etc.) to produce results directly comparable to real-world use.
//
// Build and run (from project root):
//   g++ -std=c++20 -O3 -march=native \
//       -I. -Iinclude -Ibuild/_deps/glm-src -Ibuild/_deps/fmt-src/include \
//       -Ivcpkg_installed/x64-linux/include \
//       tests/bench/effect_dispatch_benchmark.cpp \
//       vcpkg_installed/x64-linux/lib/libbenchmark.a \
//       vcpkg_installed/x64-linux/lib/libbenchmark_main.a \
//       -lpthread -ldl -o /tmp/effect_dispatch_bench
//   /tmp/effect_dispatch_bench --benchmark_min_time=0.5s

#include <benchmark/benchmark.h>

#include <any>
#include <variant>
#include <vector>
#include <cstdint>
#include <cstring>
#include <typeindex>
#include <random>
#include <array>

// ── Effect parameter types (copy of the real types from the project) ──────

struct BlurParams          { float radius{0.0f}; };
struct TintParams          { struct { float r,g,b,a; } color{0,0,0,0}; float amount{1.0f}; };
struct BrightnessParams    { float value{0.0f}; };
struct ContrastParams      { float value{1.0f}; };
struct DropShadowParams    { struct { float x,y; } offset{0,8}; struct { float r,g,b,a; } color{0,0,0,0.35f}; float radius{12}; };
struct GlowParams          { float radius{15.0f}; float intensity{0.8f}; };
struct BloomParams         { float threshold{0.80f}; float radius{24.0f}; float intensity{0.60f}; };
struct Fake3DWaveParams    { float amplitude_px{18.0f}; float frequency{2.2f}; float speed{3.5f}; };

// ── Variant-based storage (NEW) ───────────────────────────────────────────

enum class EffectType : uint8_t {
    Unknown = 0, Blur, Tint, Brightness, Contrast, DropShadow, Glow, Bloom, Fake3DWave
};

using EffectParams = std::variant<
    std::monostate, BlurParams, TintParams, BrightnessParams, ContrastParams,
    DropShadowParams, GlowParams, BloomParams, Fake3DWaveParams
>;

struct InstanceVariant {
    EffectType   effect_type{EffectType::Unknown};
    EffectParams params;
    bool         enabled{true};
};

// ── std::any-based storage (OLD) ──────────────────────────────────────────

struct InstanceAny {
    EffectType effect_type{EffectType::Unknown};
    std::any   params;
    bool       enabled{true};
};

// ── Helper: create a batch of instances for the benchmark ─────────────────

template <typename T>
std::vector<T> create_batch(size_t count, std::mt19937& rng) {
    std::vector<T> instances;
    instances.reserve(count);

    std::array<EffectType, 8> types = {
        EffectType::Blur, EffectType::Tint, EffectType::Brightness,
        EffectType::Contrast, EffectType::DropShadow, EffectType::Glow,
        EffectType::Bloom, EffectType::Fake3DWave
    };
    std::uniform_int_distribution<int> dist(0, 7);

    for (size_t i = 0; i < count; ++i) {
        EffectType et = types[dist(rng)];
        T inst;
        inst.effect_type = et;
        inst.enabled = true;

        switch (et) {
            case EffectType::Blur:        inst.params = BlurParams{static_cast<float>(i % 100)}; break;
            case EffectType::Tint:        inst.params = TintParams{}; break;
            case EffectType::Brightness:  inst.params = BrightnessParams{0.5f}; break;
            case EffectType::Contrast:    inst.params = ContrastParams{1.5f}; break;
            case EffectType::DropShadow:  inst.params = DropShadowParams{}; break;
            case EffectType::Glow:        inst.params = GlowParams{10.0f, 0.5f}; break;
            case EffectType::Bloom:       inst.params = BloomParams{0.8f, 24.0f, 0.6f}; break;
            case EffectType::Fake3DWave:  inst.params = Fake3DWaveParams{}; break;
            default: break;
        }
        instances.push_back(std::move(inst));
    }
    return instances;
}

// ── OLD dispatch: chain of std::any_cast with runtime type_info lookup ────

struct DispatchResult {
    EffectType type;
    float      value;
};

inline DispatchResult dispatch_old(const InstanceAny& inst) {
    DispatchResult r;
    r.type = inst.effect_type;
    r.value = 0.0f;

    // Simulates the exact chain pattern from effect_stack.cpp (8 any_cast calls)
    if (auto* p = std::any_cast<BlurParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::any_cast<TintParams>(&inst.params)) {
        r.value = p->amount;
    } else if (auto* p = std::any_cast<BrightnessParams>(&inst.params)) {
        r.value = p->value;
    } else if (auto* p = std::any_cast<ContrastParams>(&inst.params)) {
        r.value = p->value;
    } else if (auto* p = std::any_cast<DropShadowParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::any_cast<GlowParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::any_cast<BloomParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::any_cast<Fake3DWaveParams>(&inst.params)) {
        r.value = p->amplitude_px;
    }
    return r;
}

// ── NEW dispatch: std::get_if via variant index (O(1)) ────────────────────

inline DispatchResult dispatch_new(const InstanceVariant& inst) {
    DispatchResult r;
    r.type = inst.effect_type;
    r.value = 0.0f;

    // std::get_if<T> uses the variant's type index — O(1), no string/type_info compare
    if (auto* p = std::get_if<BlurParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::get_if<TintParams>(&inst.params)) {
        r.value = p->amount;
    } else if (auto* p = std::get_if<BrightnessParams>(&inst.params)) {
        r.value = p->value;
    } else if (auto* p = std::get_if<ContrastParams>(&inst.params)) {
        r.value = p->value;
    } else if (auto* p = std::get_if<DropShadowParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::get_if<GlowParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::get_if<BloomParams>(&inst.params)) {
        r.value = p->radius;
    } else if (auto* p = std::get_if<Fake3DWaveParams>(&inst.params)) {
        r.value = p->amplitude_px;
    }
    return r;
}

// ── Switch-on-effect_type dispatch (alternative pattern) ──────────────────

inline DispatchResult dispatch_switch(const InstanceVariant& inst) {
    DispatchResult r;
    r.type = inst.effect_type;
    r.value = 0.0f;

    switch (inst.effect_type) {
        case EffectType::Blur: {
            auto* p = std::get_if<BlurParams>(&inst.params);
            if (p) r.value = p->radius;
            break;
        }
        case EffectType::Tint: {
            auto* p = std::get_if<TintParams>(&inst.params);
            if (p) r.value = p->amount;
            break;
        }
        case EffectType::Brightness: {
            auto* p = std::get_if<BrightnessParams>(&inst.params);
            if (p) r.value = p->value;
            break;
        }
        case EffectType::Contrast: {
            auto* p = std::get_if<ContrastParams>(&inst.params);
            if (p) r.value = p->value;
            break;
        }
        case EffectType::DropShadow: {
            auto* p = std::get_if<DropShadowParams>(&inst.params);
            if (p) r.value = p->radius;
            break;
        }
        case EffectType::Glow: {
            auto* p = std::get_if<GlowParams>(&inst.params);
            if (p) r.value = p->radius;
            break;
        }
        case EffectType::Bloom: {
            auto* p = std::get_if<BloomParams>(&inst.params);
            if (p) r.value = p->radius;
            break;
        }
        case EffectType::Fake3DWave: {
            auto* p = std::get_if<Fake3DWaveParams>(&inst.params);
            if (p) r.value = p->amplitude_px;
            break;
        }
        default: break;
    }
    return r;
}

// ── Benchmark: any_cast chain ─────────────────────────────────────────────

static void BM_AnyCastChain(benchmark::State& state) {
    const size_t batch_size = static_cast<size_t>(state.range(0));
    std::mt19937 rng(42);
    auto instances = create_batch<InstanceAny>(batch_size, rng);
    size_t idx = 0;

    for (auto _ : state) {
        idx = (idx + 1) & (batch_size - 1);
        auto r = dispatch_old(instances[idx]);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_AnyCastChain)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kNanosecond);

// ── Benchmark: variant get_if chain ───────────────────────────────────────

static void BM_VariantGetIf(benchmark::State& state) {
    const size_t batch_size = static_cast<size_t>(state.range(0));
    std::mt19937 rng(42);
    auto instances = create_batch<InstanceVariant>(batch_size, rng);
    size_t idx = 0;

    for (auto _ : state) {
        idx = (idx + 1) & (batch_size - 1);
        auto r = dispatch_new(instances[idx]);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_VariantGetIf)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kNanosecond);

// ── Benchmark: switch + get_if (ideal pattern) ────────────────────────────

static void BM_VariantSwitch(benchmark::State& state) {
    const size_t batch_size = static_cast<size_t>(state.range(0));
    std::mt19937 rng(42);
    auto instances = create_batch<InstanceVariant>(batch_size, rng);
    size_t idx = 0;

    for (auto _ : state) {
        idx = (idx + 1) & (batch_size - 1);
        auto r = dispatch_switch(instances[idx]);
        benchmark::DoNotOptimize(r);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_VariantSwitch)->Arg(64)->Arg(256)->Arg(1024)->Unit(benchmark::kNanosecond);

// ── Full effect-stack simulation ──────────────────────────────────────────
// Process all 8 effect types in sequence (as in apply_effect_stack), measuring
// total dispatch time for the entire stack.

static void BM_FullEffectStackAny(benchmark::State& state) {
    // One instance of each effect type, processed in a loop (as in effect_stack.cpp)
    std::array<InstanceAny, 8> stack;
    stack[0].effect_type = EffectType::Blur;        stack[0].params = BlurParams{10.0f};
    stack[1].effect_type = EffectType::Tint;        stack[1].params = TintParams{};
    stack[2].effect_type = EffectType::Brightness;   stack[2].params = BrightnessParams{0.5f};
    stack[3].effect_type = EffectType::Contrast;     stack[3].params = ContrastParams{1.5f};
    stack[4].effect_type = EffectType::DropShadow;   stack[4].params = DropShadowParams{};
    stack[5].effect_type = EffectType::Glow;         stack[5].params = GlowParams{15.0f, 0.8f};
    stack[6].effect_type = EffectType::Bloom;        stack[6].params = BloomParams{};
    stack[7].effect_type = EffectType::Fake3DWave;   stack[7].params = Fake3DWaveParams{};

    for (auto _ : state) {
        DispatchResult results[8];
        for (int i = 0; i < 8; ++i) {
            results[i] = dispatch_old(stack[i]);
        }
        benchmark::DoNotOptimize(results);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 8);
}
BENCHMARK(BM_FullEffectStackAny)->Unit(benchmark::kNanosecond);

static void BM_FullEffectStackVariant(benchmark::State& state) {
    std::array<InstanceVariant, 8> stack;
    stack[0].effect_type = EffectType::Blur;        stack[0].params = BlurParams{10.0f};
    stack[1].effect_type = EffectType::Tint;        stack[1].params = TintParams{};
    stack[2].effect_type = EffectType::Brightness;   stack[2].params = BrightnessParams{0.5f};
    stack[3].effect_type = EffectType::Contrast;     stack[3].params = ContrastParams{1.5f};
    stack[4].effect_type = EffectType::DropShadow;   stack[4].params = DropShadowParams{};
    stack[5].effect_type = EffectType::Glow;         stack[5].params = GlowParams{15.0f, 0.8f};
    stack[6].effect_type = EffectType::Bloom;        stack[6].params = BloomParams{};
    stack[7].effect_type = EffectType::Fake3DWave;   stack[7].params = Fake3DWaveParams{};

    for (auto _ : state) {
        DispatchResult results[8];
        for (int i = 0; i < 8; ++i) {
            results[i] = dispatch_new(stack[i]);
        }
        benchmark::DoNotOptimize(results);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 8);
}
BENCHMARK(BM_FullEffectStackVariant)->Unit(benchmark::kNanosecond);

static void BM_FullEffectStackSwitch(benchmark::State& state) {
    std::array<InstanceVariant, 8> stack;
    stack[0].effect_type = EffectType::Blur;        stack[0].params = BlurParams{10.0f};
    stack[1].effect_type = EffectType::Tint;        stack[1].params = TintParams{};
    stack[2].effect_type = EffectType::Brightness;   stack[2].params = BrightnessParams{0.5f};
    stack[3].effect_type = EffectType::Contrast;     stack[3].params = ContrastParams{1.5f};
    stack[4].effect_type = EffectType::DropShadow;   stack[4].params = DropShadowParams{};
    stack[5].effect_type = EffectType::Glow;         stack[5].params = GlowParams{15.0f, 0.8f};
    stack[6].effect_type = EffectType::Bloom;        stack[6].params = BloomParams{};
    stack[7].effect_type = EffectType::Fake3DWave;   stack[7].params = Fake3DWaveParams{};

    for (auto _ : state) {
        DispatchResult results[8];
        for (int i = 0; i < 8; ++i) {
            results[i] = dispatch_switch(stack[i]);
        }
        benchmark::DoNotOptimize(results);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 8);
}
BENCHMARK(BM_FullEffectStackSwitch)->Unit(benchmark::kNanosecond);

// ── Benchmark registration ────────────────────────────────────────────────

BENCHMARK_MAIN();
