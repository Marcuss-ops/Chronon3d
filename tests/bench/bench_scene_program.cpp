// =============================================================================
// bench_scene_program.cpp — B7: Scene program compilation & refresh benchmarks
//
/// Measures the throughput of scene compilation, binding table construction,
/// and parameter refresh — the three key operations in the program lifecycle.
///
/// Benchmarks:
///   1. BM_ProgramCompile       — Compile a multi-layer scene from scratch
///   2. BM_BindingBuild         — Build the binding table from compiled graph
///   3. BM_ProgramRefresh       — Refresh program params for a new frame
///   4. BM_ProgramCacheHit      — Lookup a cached program by key
///   5. BM_FrameParamBlockWarmUp — Warm up a FrameParameterBlock with N entries
///
/// Each benchmark reports ItemsProcessed (scene count for compile; binding
/// entries for build/refresh) and a custom "fps" counter.
// =============================================================================

#include <benchmark/benchmark.h>

#include <chronon3d/render_graph/cache/scene_program_cache.hpp>
#include <chronon3d/render_graph/compiler/compiled_scene_program.hpp>
#include <chronon3d/render_graph/compiler/scene_binding.hpp>
#include <chronon3d/render_graph/pipeline/frame_parameter_block.hpp>
#include <chronon3d/render_graph/render_graph.hpp>
#include <chronon3d/render_graph/core/render_graph_hashing.hpp>
#include <chronon3d/internal/render_graph/core/scene_hasher.hpp>
#include <chronon3d/render_graph/nodes/source_node.hpp>
#include <chronon3d/render_graph/nodes/transform_node.hpp>
#include <chronon3d/render_graph/nodes/effect_stack_node.hpp>
#include <chronon3d/scene/model/core/scene.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>

#include <memory>
#include <random>
using namespace chronon3d;

using namespace chronon3d::graph;

namespace {

// ── Scene generator ─────────────────────────────────────────────────────────
// Builds a test scene with `num_layers` layers, each with 1-3 effects.
// Produces deterministic output for the same `num_layers` input.

Scene make_test_scene(int num_layers) {
    SceneBuilder builder;

    // Background layer (always present)
    builder.rect("bg", {.size = {1920, 1080}, .color = Color::gray()});

    for (int i = 0; i < num_layers; ++i) {
        const std::string layer_name = "layer_" + std::to_string(i);
        builder.layer(layer_name, [i](LayerBuilder& lb) {
            lb.rect("box_" + std::to_string(i),
                    {.size = {100, 100},
                     .color = Color{0.3f + 0.1f * i,
                                    0.2f + 0.1f * i,
                                    0.4f + 0.1f * i,
                                    1.0f},
                     .pos = {100.0f + 50.0f * i,
                             100.0f + 50.0f * i, 0}});
            // Add effects on even layers
            if (i % 2 == 0) {
                lb.blur(5.0f);
                lb.brightness(0.1f);
            }
        });
    }

    return builder.build();
}

/// Build a compiled frame graph with binding metadata for a test scene.
CompiledFrameGraph make_compiled_graph(int num_layers) {
    auto scene = make_test_scene(num_layers);

    RenderGraph graph;
    std::vector<GraphNodeId> ids;

    // Create nodes for each layer
    for (int i = 0; i < num_layers; ++i) {
        const std::string layer_id = "layer_" + std::to_string(i);

        auto src_id = graph.add_node(std::make_unique<SourceNode>(
            layer_id, RenderNode{}, cache::NodeCacheKey{}, false));
        auto xform_id = graph.add_node(std::make_unique<TransformNode>(
            Transform{}));
        ids.push_back(src_id);
        ids.push_back(xform_id);
        graph.connect(src_id, xform_id);

        if (i % 2 == 0) {
            auto effect_id = graph.add_node(std::make_unique<EffectStackNode>(
                EffectStack{}, layer_id, Frame{0}));
            graph.connect(xform_id, effect_id);
            ids.push_back(effect_id);
        }
    }

    if (!ids.empty()) {
        graph.set_output(ids.back());
    }

    CompiledFrameGraph compiled;
    compiled.graph = std::move(graph);
    compiled.output = ids.empty() ? k_invalid_node : ids.back();
    compiled.valid = true;

    // Build levels and node info
    compiled.nodes.resize(compiled.graph.size());
    for (GraphNodeId id = 0; id < compiled.graph.size(); ++id) {
        if (!compiled.graph.has_node(id)) continue;
        compiled.nodes[id].id = id;
        compiled.nodes[id].reachable = true;
    }

    // Assign binding metadata and kind based on node indices
    int layer_idx = 0;
    GraphNodeId node_idx = 0;
    for (int i = 0; i < num_layers; ++i) {
        const std::string layer_id = "layer_" + std::to_string(i);

        // Find source node for this layer
        while (node_idx < compiled.nodes.size() &&
               (!compiled.graph.has_node(node_idx) ||
                compiled.nodes[node_idx].kind == RenderGraphNodeKind::Source)) {
            if (compiled.graph.has_node(node_idx)) {
                auto* node = dynamic_cast<SourceNode*>(&compiled.graph.node(node_idx));
                if (node && node->layer_id() == layer_id) {
                    compiled.nodes[node_idx].kind = RenderGraphNodeKind::Source;
                    compiled.nodes[node_idx].binding_meta = {true,
                        static_cast<uint32_t>(layer_idx), 0, 0, 0};
                }
            }
            ++node_idx;
        }
        ++layer_idx;
    }

    return compiled;
}

/// A simple counter-based compiler for SceneProgramCache benchmarks.
struct CounterCompiler {
    std::atomic<int>& call_count;

    std::unique_ptr<CompiledSceneProgram> operator()() {
        call_count.fetch_add(1, std::memory_order_relaxed);

        auto program = std::make_unique<CompiledSceneProgram>();
        auto& graph = program->frame_graph.graph;
        auto src_id = graph.add_node(std::make_unique<TransformNode>(
            Transform{}));
        graph.set_output(src_id);

        program->frame_graph.output = src_id;
        program->frame_graph.levels = {{src_id}};
        program->frame_graph.valid = true;
        program->valid = true;
        return program;
    }
};

/// Make a SceneStructureKey from a seed value.
SceneStructureKey make_key(uint64_t topology, int w = 1920, int h = 1080) {
    return SceneStructureKey{
        .topology_hash       = topology,
        .active_set_hash     = 0,
        .render_options_hash = hash_combine(0, 1),
        .width               = w,
        .height              = h,
        .ssaa_factor         = 1
    };
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// BM_ProgramCompile — Full program compilation from scene
// ══════════════════════════════════════════════════════════════════════════

static void BM_ProgramCompile(benchmark::State& state) {
    const int num_layers = static_cast<int>(state.range(0));

    for (auto _ : state) {
        auto scene = make_test_scene(num_layers);
        auto compiled = make_compiled_graph(num_layers);
        auto program = compile_scene_program(std::move(compiled));
        benchmark::DoNotOptimize(program.bindings.size());
    }

    state.counters["layers"] = static_cast<double>(num_layers);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * num_layers);
}

BENCHMARK(BM_ProgramCompile)
    ->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(32)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_BindingBuild — Build binding table from compiled graph
// ══════════════════════════════════════════════════════════════════════════

static void BM_BindingBuild(benchmark::State& state) {
    const int num_layers = static_cast<int>(state.range(0));
    auto compiled = make_compiled_graph(num_layers);

    for (auto _ : state) {
        auto bindings = build_binding_table(compiled);
        benchmark::DoNotOptimize(bindings.size());
    }

    state.counters["bindings"] = static_cast<double>(num_layers);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * num_layers);
}

BENCHMARK(BM_BindingBuild)
    ->Arg(1)->Arg(4)->Arg(8)->Arg(16)->Arg(32)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_ProgramRefresh — Refresh program parameters for a new frame
// ══════════════════════════════════════════════════════════════════════════

static void BM_ProgramRefresh(benchmark::State& state) {
    const int num_bindings = static_cast<int>(state.range(0));
    FrameParameterBlock block;
    block.warm_up(static_cast<size_t>(num_bindings));

    for (auto _ : state) {
        block.begin_frame();
        for (int i = 0; i < num_bindings; ++i) {
            block[i].opacity = 0.75f;
            block[i].matrix = Mat4{1.0f};
            block[i].refreshed_this_frame = true;
        }
        block.refresh_count = static_cast<uint64_t>(num_bindings);
        benchmark::DoNotOptimize(block.refresh_count);
        benchmark::ClobberMemory();
    }

    state.counters["entries"] = static_cast<double>(num_bindings);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * num_bindings);
}

BENCHMARK(BM_ProgramRefresh)
    ->Arg(4)->Arg(16)->Arg(64)->Arg(256)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_ProgramCacheHit — Lookup cached program by key
// ══════════════════════════════════════════════════════════════════════════

static void BM_ProgramCacheHit(benchmark::State& state) {
    cache::SceneProgramCache cache(32);
    std::atomic<int> compile_count{0};

    // Pre-populate cache with 16 entries.
    for (int i = 0; i < 16; ++i) {
        cache.find_or_compile(make_key(100 + i),
                              CounterCompiler{compile_count});
    }

    uint64_t key_idx = 0;

    for (auto _ : state) {
        // Cycle through keys for realistic hit/miss pattern
        const uint64_t topo = 100 + (key_idx % 16);
        auto* program = cache.find(make_key(topo));
        benchmark::DoNotOptimize(program);
        ++key_idx;
    }

    state.counters["cache_size"] = static_cast<double>(cache.size());
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_ProgramCacheHit)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_ProgramCacheMiss — Compile on cache miss
// ══════════════════════════════════════════════════════════════════════════

static void BM_ProgramCacheMiss(benchmark::State& state) {
    cache::SceneProgramCache cache(32);
    std::atomic<int> compile_count{0};
    uint64_t unique_key = 10000;

    for (auto _ : state) {
        auto* program = cache.find_or_compile(
            make_key(unique_key++),
            CounterCompiler{compile_count});
        benchmark::DoNotOptimize(program);
    }

    state.counters["compilations"] = static_cast<double>(compile_count.load());
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_ProgramCacheMiss)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_FrameParamBlockWarmUp — Warm up FrameParameterBlock with N entries
// ══════════════════════════════════════════════════════════════════════════

static void BM_FrameParamBlockWarmUp(benchmark::State& state) {
    const int num_entries = static_cast<int>(state.range(0));

    for (auto _ : state) {
        FrameParameterBlock block;
        block.warm_up(static_cast<size_t>(num_entries));
        benchmark::DoNotOptimize(block.size());
        benchmark::ClobberMemory();
    }

    state.counters["entries"] = static_cast<double>(num_entries);
}

BENCHMARK(BM_FrameParamBlockWarmUp)
    ->Arg(4)->Arg(16)->Arg(64)->Arg(256)->Arg(1024)
    ->Unit(benchmark::kMicrosecond);

// ══════════════════════════════════════════════════════════════════════════
// BM_SceneStructureKeyHash — Hash throughput for SceneStructureKey
// ══════════════════════════════════════════════════════════════════════════

static void BM_SceneStructureKeyHash(benchmark::State& state) {
    cache::SceneProgramCache cache(8);
    std::hash<SceneStructureKey> hasher;

    // Pre-generate keys
    std::vector<SceneStructureKey> keys;
    keys.reserve(128);
    for (int i = 0; i < 128; ++i) {
        keys.push_back(make_key(1000 + i));
    }

    size_t idx = 0;
    for (auto _ : state) {
        auto h = hasher(keys[idx % 128]);
        benchmark::DoNotOptimize(h);
        ++idx;
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

BENCHMARK(BM_SceneStructureKeyHash)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
