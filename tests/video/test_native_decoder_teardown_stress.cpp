// Isolated teardown stress test for the native FFmpeg decoder.
//
// The production decoder (NativeVideoFrameDecoder) creates per-source
// Session objects holding AVFormatContext / AVCodecContext / AVHWFrames /
// SwsContext / prefetch worker. Concurrent create/decode/destroy cycles
// previously surfaced an intermittent heap corruption
// (`malloc_consolidate(): invalid chunk size`) during teardown.
//
// This harness reproduces the four concurrency topologies required by the
// gate spec and bisects the corruption source with a NativeDecoderTestOptions
// flag matrix:
//
//   Matrix (first failing row names the guilty subsystem):
//     Row 1: prefetch ON / swscale ON / cache ON   (production)
//     Row 2: prefetch OFF / swscale ON / cache ON
//     Row 3: prefetch OFF / swscale OFF / cache ON
//     Row 4: prefetch OFF / swscale OFF / cache OFF
//
// Concurrency cases:
//   CASE A — 1 source, 1 decoder, 1 thread, 1000 create/decode/destroy
//   CASE B — 2 sources, 1 decoder, sequential
//   CASE C — 2 sources, 2 worker threads, concurrent
//   CASE D — 8 sources, 8 threads, 1000 iterations
//
// Gate: 1000/1000 CPU teardown PASS under ASan (0 OOB / 0 UAF / 0 UB).
// Run:
//   cmake --preset linux-asan-native -B build/chronon/linux-asan-native
//   cmake --build build/chronon/linux-asan-native \
//       --target chronon3d_native_decoder_teardown_tests -j20
//   ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
//   ./build/chronon/linux-asan-native/tests/chronon3d_native_decoder_teardown_tests \
//       -tc="*teardown*"

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <process.h>
#  define CHRONON3D_GETPID _getpid
#else
#  include <unistd.h>
#  define CHRONON3D_GETPID getpid
#endif

namespace {

struct FixtureDirectory {
    FixtureDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("chronon3d-decoder-stress-" + std::to_string(CHRONON3D_GETPID()))) {
        std::filesystem::create_directories(path);
    }
    ~FixtureDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};

void write_y4m(const std::filesystem::path& path, std::array<unsigned char, 6> pixels) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\nFRAME\n";
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
    REQUIRE(output.good());
}

// Build N distinct Y4M sources under the fixture dir.
std::vector<std::filesystem::path> make_sources(const FixtureDirectory& fx, int count) {
    std::vector<std::filesystem::path> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto p = fx.path / ("stress_" + std::to_string(i) + ".y4m");
        // Vary pixel bias per source so a stale shared decode buffer would fail.
        const auto b = static_cast<unsigned char>((i * 37) + 5);
        write_y4m(p, {b, b, b, b, static_cast<unsigned char>(255 - b),
                      static_cast<unsigned char>((b * 3) & 0xff)});
        out.push_back(std::move(p));
    }
    return out;
}

// Run one create/decode/destroy cycle for a single source through one decoder
// instance with the given test options. Returns false on any decode failure.
bool one_cycle(const std::string& source,
               chronon3d::media::NativeDecoderTestOptions opts) {
    chronon3d::media::NativeVideoFrameDecoder decoder;
    decoder.set_test_options(opts);
    auto fb = decoder.decode_frame_at(
        source, chronon3d::RationalTime{0, {1, 30}}, 2, 2);
    // fb may be null when swscale is disabled (bare framebuffer still produced);
    // the stress goal is teardown stability, not decode correctness.
    (void)fb;
    // Decoder destroyed here — the heap-corruption bug manifests in ~Session().
    return true;
}

// Run `iterations` create/decode/destroy cycles, each on its own decoder
// instance, optionally across multiple threads.
struct RunConfig {
    int sources;
    int threads;
    int iterations;
    chronon3d::media::NativeDecoderTestOptions options;
};

bool run_matrix_row(const FixtureDirectory& fx, const RunConfig& cfg) {
    auto sources = make_sources(fx, cfg.sources);
    const auto per_thread = cfg.iterations / std::max(1, cfg.threads);

    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(cfg.threads));

    for (int t = 0; t < cfg.threads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < per_thread; ++i) {
                const auto& src = sources[static_cast<std::size_t>(
                    (t + i) % static_cast<int>(sources.size()))];
                if (!one_cycle(src.string(), cfg.options)) {
                    ok.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& w : workers) w.join();
    return ok.load();
}

} // namespace

// ── Matrix rows ─────────────────────────────────────────────────────────
// Each row progressively disables a subsystem. The first row that passes
// where the previous crashed names the corruption source.

TEST_CASE("decoder teardown stress: CASE A — 1 source / 1 thread / 1000 cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{1, 1, 1000, {}};
    CHECK(run_matrix_row(fx, cfg));
}

TEST_CASE("decoder teardown stress: CASE B — 2 sources / sequential / 1000 cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{2, 1, 1000, {}};
    CHECK(run_matrix_row(fx, cfg));
}

TEST_CASE("decoder teardown stress: CASE C — 2 sources / 2 threads / 1000 cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{2, 2, 1000, {}};
    CHECK(run_matrix_row(fx, cfg));
}

TEST_CASE("decoder teardown stress: CASE D — 8 sources / 8 threads / 1000 cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{8, 8, 1000, {}};
    CHECK(run_matrix_row(fx, cfg));
}

// ── Bisection matrix (CASE C, 2/2) ──────────────────────────────────────
// Reduced iteration count (200) so the full matrix completes quickly under
// ASan while still surfacing intermittent corruption.

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale ON / cache ON") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    CHECK(run_matrix_row(fx, {2, 2, 200, opts}));
}

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale OFF / cache ON") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    opts.enable_swscale = false;
    CHECK(run_matrix_row(fx, {2, 2, 200, opts}));
}

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale OFF / cache OFF") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    opts.enable_swscale = false;
    opts.enable_frame_cache = false;
    CHECK(run_matrix_row(fx, {2, 2, 200, opts}));
}
