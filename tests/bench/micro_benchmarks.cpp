#include <chronon3d/scene/builders/scene_builder.hpp>
#include <benchmark/benchmark.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/simd/kernels.hpp>
#include <chronon3d/cache/framebuffer_pool.hpp>
#include <chronon3d/cache/persistent_framebuffer_store.hpp>
#include <chronon3d/effects/glow_pipeline.hpp>
#include <chronon3d/media/frame_conversion/converted_frame_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
// TICKET-O(n)-audit — micro-benchmark setup uses unordered_set / vector.
#include <unordered_set>
#include <cstdint>
#include <algorithm>  // std::min_element, std::find
#include <limits>      // std::numeric_limits

// Include the real blur implementation for proper benchmarks
#include "src/backends/software/utils/render_effects_processor.hpp"

// Tile benchmark includes
#include <chronon3d/api/composition.hpp>
#include <chronon3d/api/scene.hpp>
#include <chronon3d/api/renderer.hpp>
#include <chronon3d/core/types/frame_context.hpp>
#include <chronon3d/timeline/composition.hpp>

#include <chronon3d/core/profiling/profiling.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <tests/helpers/test_utils.hpp>
using namespace chronon3d;

using namespace chronon3d::video;
using namespace chronon3d::renderer;

namespace {

namespace glow_bench {

constexpr int kBenchRoiW = 340;
constexpr int kBenchRoiH = 220;
constexpr int kBenchPxCount = kBenchRoiW * kBenchRoiH;

void populate_text_shaped_alpha_mask(Framebuffer& fb) {
    fb.clear(Color::transparent());
    // Two stacked radial gradients at the centre plus a thin top/bottom band
    // to imitate the ink distribution of a single-line caption rasterised at
    // ~110px Georgia/Inter Bold.
    const float cx = static_cast<float>(kBenchRoiW) * 0.5f;
    const float cy = static_cast<float>(kBenchRoiH) * 0.5f;
    const float r_outer = std::min(cx, cy) * 0.95f;
    for (int y = 0; y < kBenchRoiH; ++y) {
        for (int x = 0; x < kBenchRoiW; ++x) {
            const float dx = static_cast<float>(x) - cx;
            const float dy = static_cast<float>(y) - cy;
            const float r = std::sqrt(dx * dx + std::pow(dy * 2.0f, 2.0f));
            const float a = std::clamp(1.0f - r / r_outer, 0.0f, 1.0f);
            if (a > 0.0f) {
                fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, a});
            }
        }
    }
    // Sprinkle in some sparse periphery "anti-aliased edge" pixels at low alpha
    // to exercise the LUT at the bottom entries as well.
    for (int i = 0; i < kBenchRoiW * 4; ++i) {
        const int x = (i * 7) % kBenchRoiW;
        const int y = (i * 11) % kBenchRoiH;
        const float existing = fb.get_pixel(x, y).a;
        if (existing == 0.0f) {
            fb.set_pixel(x, y, Color{1.0f, 1.0f, 1.0f, 12.0f / 255.0f});
        }
    }
}

// Acquire a framebuffer through the pool, falling back to a heap alloc if no
// pool is currently installed on the profiling thread-local.
std::shared_ptr<Framebuffer> acquire_pooled(int w, int h) {
    if (profiling::g_current_framebuffer_pool) {
        return profiling::g_current_framebuffer_pool->acquire_pooled(
            w, h,
            profiling::g_current_framebuffer_pool->shared_from_this(),
            /*cleared=*/true
        );
    }
    auto fb = std::make_shared<Framebuffer>(w, h);
    fb->clear(Color::transparent());
    return fb;
}

} // namespace glow_bench

// BENCHMARK_CAPTURE picks the falloff value at registration time so we can
// compare LUT-path vs identity-skip-path on the same machine.
static void BM_GlowLayerPass(benchmark::State& state, float falloff) {
    using namespace chronon3d::renderer;
    using namespace glow_bench;

    // ── Pool warmup: 4 buffers recycled through the pool, then attached as
    // the profiling thread-local so acquire_temp_framebuffer routes here.
    auto pool = std::make_shared<cache::FramebufferPool>(/*capacity=*/16);
    profiling::g_current_framebuffer_pool = pool.get();
    auto warmup = acquire_pooled(kBenchRoiW, kBenchRoiH);
    auto src_fb  = acquire_pooled(kBenchRoiW, kBenchRoiH);
    auto dst_fb  = acquire_pooled(kBenchRoiW, kBenchRoiH);
    populate_text_shaped_alpha_mask(*src_fb);

    // ── Build the falloff LUT once per benchmark instance (per the patched
    // glow pipeline: build_falloff_lut is called inside build_glow_accumulator).
    float falloff_lut[kFalloffLutSize];
    build_falloff_lut(falloff, falloff_lut);

    // ── GlowPipeline params matching the SpecialName default (radius=18,
    // color=white-blue, intensity=0.25).  We don't need the blur stage —
    // accumulate_glow_pass is the inner hot loop we're benching.
    GlowPipeline p{};
    p.appearance.color = {0.80f, 0.85f, 1.0f, 1.0f};
    p.radius = 18.0f;
    p.intensity = 0.25f;
    p.falloff = falloff;
    p.softness = 1.0f;
    p.core_strength = 0.70f;
    p.aura_strength = 0.35f;
    p.bloom_strength = 0.18f;

    // Discard warmup result (first TBB partition may pre-allocate worker
    // structures; we don't want that cost in the timed loop).
    (void)warmup;

    for (auto _ : state) {
        dst_fb->clear(Color::transparent());
        accumulate_glow_pass(*dst_fb, *src_fb, p, falloff_lut);
        benchmark::DoNotOptimize(dst_fb->get_pixel(0, 0));
    }

    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kBenchPxCount);
    state.counters["roi_w"] = kBenchRoiW;
    state.counters["roi_h"] = kBenchRoiH;
    state.counters["falloff"] = falloff;
    state.counters["pool_capacity"] = static_cast<double>(pool.use_count());

    profiling::g_current_framebuffer_pool = nullptr;
}

    // Only the 2 BENCHMARK_CAPTURE(BM_GlowLayerPass) registrations are exposed (visible
    // to google-benchmark's static-init registration machinery inside this anonymous
    // namespace).
    BENCHMARK_CAPTURE(BM_GlowLayerPass, Falloff085, 0.85f)
        ->Unit(benchmark::kMillisecond);
    BENCHMARK_CAPTURE(BM_GlowLayerPass, Falloff100, 1.0f)
        ->Unit(benchmark::kMillisecond);

    // ----------------------------------------------------------------------
    // TICKET-O(n)-audit — micro-benchmarks (before vs after) for the three
    // sites that switched from O(n) std::find / hand-rolled linear scans to
    // std::unordered_set lookup or std::min_element.  Bit-identical
    // behaviour; the bench exists so future regressions on the same shape
    // are detectable.
    // ----------------------------------------------------------------------
    namespace onshore {

    constexpr int kBenchN = 100'000;  // 100k-entry workload per follow-up brief.

    // Site 1: vector vs unordered_set membership (Entry* identity pattern).
    void BM_Site1_Lookup(benchmark::State& state, bool use_set) {
        std::vector<int> vec;
        std::unordered_set<int> set;
        vec.reserve(static_cast<size_t>(kBenchN));
        for (int i = 0; i < kBenchN; ++i) {
            vec.push_back(i);
            set.insert(i);
        }
        const int probe = kBenchN - 1;  // worst-case scan length.
        for (auto _ : state) {
            if (use_set) {
                benchmark::DoNotOptimize(set.find(probe));
            } else {
                benchmark::DoNotOptimize(std::find(vec.begin(), vec.end(), probe));
            }
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    }

    // Site 2: vector<string> vs unordered_set<string> membership.
    void BM_Site2_StringDedup(benchmark::State& state, bool use_set) {
        std::vector<std::string> vec;
        std::unordered_set<std::string> set;
        vec.reserve(static_cast<size_t>(kBenchN));
        for (int i = 0; i < kBenchN; ++i) {
            const auto s = std::string{"reason_"} + std::to_string(i);
            vec.push_back(s);
            set.insert(s);
        }
        const std::string probe = std::string{"reason_"} + std::to_string(kBenchN - 1);
        for (auto _ : state) {
            if (use_set) {
                benchmark::DoNotOptimize(set.count(probe));
            } else {
                benchmark::DoNotOptimize(std::find(vec.begin(), vec.end(), probe));
            }
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    }

    // Site 3: hand-rolled min-tick linear scan vs std::min_element.
    // Per-bucket sizes in production are bounded (~8-16 by
    // max_buffers_per_size_class); the bench uses a large bucket to expose
    // any genuine compiler-codegen gap.  Real win is readability.
    struct BenchEntry {
        std::uint64_t tick{0};
    };

    void BM_Site3_LRU(benchmark::State& state, bool use_stl) {
        std::vector<BenchEntry> bucket;
        bucket.resize(static_cast<size_t>(kBenchN));
        for (int i = 0; i < kBenchN; ++i) {
            bucket[i].tick = static_cast<std::uint64_t>(i);
        }
        for (auto _ : state) {
            if (use_stl) {
                auto it = std::min_element(
                    bucket.begin(), bucket.end(),
                    [](const BenchEntry& a, const BenchEntry& b) noexcept {
                        return a.tick < b.tick;
                    });
                benchmark::DoNotOptimize(it);
            } else {
                std::uint64_t min_t = std::numeric_limits<std::uint64_t>::max();
                std::size_t  idx   = 0;
                for (std::size_t i = 0; i < bucket.size(); ++i) {
                    if (bucket[i].tick < min_t) {
                        min_t = bucket[i].tick;
                        idx   = i;
                    }
                }
                benchmark::DoNotOptimize(idx);
            }
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    }

    } // namespace onshore

    // TICKET-O(n)-audit registrations — exposed to google-benchmark's
    // static-init registration machinery inside this anonymous namespace.
    BENCHMARK_CAPTURE(BM_Site1_Lookup, VectorFind,  false);
    BENCHMARK_CAPTURE(BM_Site1_Lookup, UnorderedSet, true);

    BENCHMARK_CAPTURE(BM_Site2_StringDedup, VectorFind,  false);
    BENCHMARK_CAPTURE(BM_Site2_StringDedup, UnorderedSet, true);

    BENCHMARK_CAPTURE(BM_Site3_LRU, HandRolled,    false);
    BENCHMARK_CAPTURE(BM_Site3_LRU, StdMinElement, true);

    // ----------------------------------------------------------------------
    // PersistentFramebufferStore read-microbench.
    //
    // Compares the two reader paths on the same 16-frame 1920x1080 RGBA8
    // fixture (1 entry ≈ 7.9 MB raw; total ≈ 127 MB):
    //
    //   /FOpen                — cross-platform std::ifstream path (DEFAULT,
    //                           `CHRONON3D_USE_MMAP` unset or != "1")
    //   /Mmap                 — Linux-only zero-copy ::mmap OPT-IN path
    //                           (`CHRONON3D_USE_MMAP=1`)
    //
    // Both share the same on-disk fixture (writes complete before timed
    // loop), so output bytes, checksums, and Framebuffer layouts are
    // identical — only the kernel-to-userspace transfer mechanism differs.
    // The expected win for mmap is the elimination of std::ifstream's
    // read-buffer copy + intermediate std::vector<uint8_t> allocation.
    //
    // Custom counters per Google Benchmark convention:
    //   frames              — entries sequenced per iteration (always 16)
    //   bytes_per_frame     — payload size per read (always = 1920*1080*16)
    // ----------------------------------------------------------------------
    namespace persistentfb_bench {

    constexpr int kFrameW         = 1920;
    constexpr int kFrameH         = 1080;
    constexpr int kFrames         = 16;
    constexpr int kBytesPerFrame  = kFrameW * kFrameH * static_cast<int>(sizeof(chronon3d::Color));

    // P1-13 lift: single source-of-truth for the bench's cache root.
    // Originally each function (build_fixture + the read_all_keys side)
    // computed `cache_root` independently via a static-local
    // `steady_clock::now().time_since_epoch().count()` value, which is
    // race-vulnerable if either function is invoked from a separate
    // thread / after a clock-tick boundary.  Hoisting to a single
    // namespace-scope static eliminates the dual-computation hazard:
    // both build_fixture() and read_all_keys() read the same path
    // object.  Meyers-singleton lazy init is thread-safe (C++11 §6.7).
    static const std::filesystem::path kBenchCacheRoot = []() {
        namespace fs = std::filesystem;
        return fs::temp_directory_path() /
            ("chronon3d_bench_persistentfb_" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    }();

    /// Build the 16-frame fixture under a fresh temp directory.  Lazy-init
    /// via static — runs ONCE per process; both bench variants share the
    /// same in-memory + on-disk fixture (writes complete on first call,
    /// before any timed loop starts).
    std::vector<chronon3d::cache::NodeCacheKey> build_fixture() {
        using namespace chronon3d;
        namespace fs = std::filesystem;

        const fs::path& cache_root = kBenchCacheRoot;
        std::error_code ec;
        fs::remove_all(cache_root, ec);            // start clean
        fs::create_directories(cache_root, ec);

        // P1-13 — per-bench-fresh instance (no singleton).  The store
        // shares the on-disk fixture (cache_root) with all bench iterations
        // so subsequent iterations hit the existing .cfb4 files; the
        // persistent framebuffer_store field on RenderRuntime would be
        // the production path but the micro-benchmark harness
        // intentionally avoids plumbing in a full RenderRuntime (the
        // schedule/asset/runtime pool would skew the timed-loop signal).
        static cache::PersistentFramebufferStore bench_store;
        bench_store.set_cache_dir(cache_root);
        auto& store = bench_store;

        std::vector<cache::NodeCacheKey> keys;
        keys.reserve(kFrames);
        for (int i = 0; i < kFrames; ++i) {
            // Distinct digests so the 2-level hex path sharding distributes
            // files across multiple subdirs (mirrors production layout).
            cache::NodeCacheKey key{};
            key.scope       = "fb_bench";
            key.frame       = static_cast<std::size_t>(i);
            key.width       = static_cast<std::uint32_t>(kFrameW);
            key.height      = static_cast<std::uint32_t>(kFrameH);
            key.params_hash = 0xDEADBEEFCAFE0001ULL + static_cast<std::uint64_t>(i);
            keys.push_back(key);

            // Deterministic per-pixel fill: each frame's content differs
            // in checksum so we exercise the keyed lookup path with
            // distinct payloads (not a degenerate 16× same-content case).
            auto fb = std::make_shared<Framebuffer>(kFrameW, kFrameH);
            for (int y = 0; y < kFrameH; ++y) {
                for (int x = 0; x < kFrameW; ++x) {
                    fb->set_pixel(x, y, Color{
                        static_cast<float>(x + i) * (1.0f / 4096.0f),
                        static_cast<float>(y + i) * (1.0f / 4096.0f),
                        static_cast<float>(i) * (1.0f / 16.0f),
                        1.0f
                    });
                }
            }
            auto wr = store.store(key, *fb);
            if (!wr.ok) {
                // Fixture build failed (disk full, invalid path, etc.).
                // The bench loop will still run with this key producing
                // a miss; we log once so the cause is visible in CI logs.
                spdlog::warn(
                    "[persistentfb_bench] fixture write failed for key #{}; "
                    "bench loop will emit a load miss on this entry.",
                    i);
            }
        }
        return keys;
    }

    const std::vector<chronon3d::cache::NodeCacheKey>& fixture_keys() {
        static const auto keys = build_fixture();
        return keys;
    }

    void read_all_keys() {
        using namespace chronon3d;
        // P1-13 — per-bench-fresh instance (no singleton).  The store
        // shares the cache_root pre-populated by build_fixture(); we
        // build a one-shot instance here so the bench path mirrors the
        // test harness pattern (no global state).
        static cache::PersistentFramebufferStore read_store;
        read_store.set_cache_dir(kBenchCacheRoot);
        auto& store = read_store;
        for (const auto& key : fixture_keys()) {
            auto fb = store.get(key);
            benchmark::DoNotOptimize(fb);
        }
    }

    void BM_PersistentFBRead_FOpen(benchmark::State& state) {
        (void)fixture_keys();                      // ensure fixture built
        ::setenv("CHRONON3D_USE_MMAP", "0", 1);
        for (auto _ : state) { read_all_keys(); }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kFrames);
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                                 kFrames * kBytesPerFrame);
        state.counters["frames"]          = static_cast<double>(kFrames);
        state.counters["bytes_per_frame"] = static_cast<double>(kBytesPerFrame);
    }

    void BM_PersistentFBRead_Mmap(benchmark::State& state) {
        (void)fixture_keys();
        ::setenv("CHRONON3D_USE_MMAP", "1", 1);
        for (auto _ : state) { read_all_keys(); }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kFrames);
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                                 kFrames * kBytesPerFrame);
        state.counters["frames"]          = static_cast<double>(kFrames);
        state.counters["bytes_per_frame"] = static_cast<double>(kBytesPerFrame);
    }

    } // namespace persistentfb_bench

    BENCHMARK(persistentfb_bench::BM_PersistentFBRead_FOpen)
        ->Unit(benchmark::kMillisecond)
        ->Name("PersistentFB/FOpen16x1920x1080");
    BENCHMARK(persistentfb_bench::BM_PersistentFBRead_Mmap)
        ->Unit(benchmark::kMillisecond)
        ->Name("PersistentFB/Mmap16x1920x1080");

    // ──────────────────────────────────────────────────────────────────────
    // TICKET-cluster-dedup — microbenchmark for `resolve_placed_glyph_run`.
    //
    // Before/after comparison of the cluster-dedup refactor in
    // `src/backends/text/font_engine_placed_run.cpp`:
    //   BEFORE: std::set<u32> + std::vector<u32> + std::unordered_map<u32,
    //                std::pair<size_t,size_t>>   (4 passes: set-insert /
    //                map-zip / cluster-build / per-glyph-fill).
    //   AFTER:  std::vector<u32> + std::sort + std::unique        (2 passes:
    //                sort+dedup / cluster-build-with-inline-fill).
    //
    // Fixture is a synthetic 4k-character run with each glyph mapping 1:1
    // to a byte offset — a typical ASCII path through HarfBuzz shaping.
    // Counters expose `cluster_count` (= glyph_count here) so future
    // regressions on the same shape are detectable.
    //
    // Output: ns/iter per call to `resolve_placed_glyph_run(*hb_run,
    // 0.0f, source_text)`.  Before the refactor: O(N log N) tree insert +
    // 2-extra allocations (set + map).  After: single contiguous vector +
    // single sort call.
    // ──────────────────────────────────────────────────────────────────────
    namespace placedrun_bench {

    constexpr int kRunChars = 4000;

    chronon3d::GlyphRun build_synthetic_glyph_run() {
        using namespace chronon3d;
        GlyphRun run;
        run.font_size   = 16.0f;
        run.ascent      = 16.0f;
        run.descent     = 4.0f;
        run.baseline    = 0.0f;
        run.line_height = 20.0f;
        run.width       = static_cast<float>(kRunChars) * 10.0f;
        run.glyphs.reserve(static_cast<std::size_t>(kRunChars));
        for (int i = 0; i < kRunChars; ++i) {
            GlyphPosition g;
            g.glyph_id        = static_cast<u32>(i % 128);  // pseudo-random but stable
            g.cluster         = static_cast<u32>(i);         // 1:1 with char index
            g.is_cluster_start = true;                       // every glyph IS a cluster start in this fixture
            g.advance_x       = 10.0f;
            g.advance_y       = 0.0f;
            g.x_offset        = 0.0f;
            g.y_offset        = 0.0f;
            run.glyphs.push_back(g);
        }
        return run;
    }

    const chronon3d::GlyphRun& fixture_run() {
        static const chronon3d::GlyphRun run = build_synthetic_glyph_run();
        return run;
    }

    const std::string& fixture_source_text() {
        // Source text mirror: 1 byte per glyph.
        static const std::string source(static_cast<std::size_t>(kRunChars), 'a');
        return source;
    }

    } // namespace placedrun_bench

    void BM_ResolvePlacedGlyphRun(benchmark::State& state) {
        const auto& run = placedrun_bench::fixture_run();
        const auto& source_text = placedrun_bench::fixture_source_text();
        for (auto _ : state) {
            auto placed = chronon3d::resolve_placed_glyph_run(
                run, /*tracking=*/0.0f, source_text);
            benchmark::DoNotOptimize(placed);
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                                 placedrun_bench::kRunChars);
        state.counters["cluster_count"] = static_cast<double>(placedrun_bench::kRunChars);
        state.counters["glyph_count"]   = static_cast<double>(placedrun_bench::kRunChars);
    }

    BENCHMARK(BM_ResolvePlacedGlyphRun)
        ->Unit(benchmark::kMicrosecond)
        ->Name("FontEngine/resolve_placed_glyph_run_4kchars");
} // namespace


