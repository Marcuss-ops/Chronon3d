// Isolated teardown stress test for the native FFmpeg decoder.
//
// ── The production bug (P0, 2026-09-16) ────────────────────────────────────
// A SINGLE NativeVideoFrameDecoder shared by two threads, each decoding a
// DIFFERENT source, intermittently aborts during teardown with
// `malloc_consolidate(): invalid chunk size` /
// `corrupted size vs. prev_size while consolidating` inside
// `avformat_close_input`, called from
// NativeVideoFrameDecoder::Session::~Session
// (src/media/video/native_video_frame_decoder_session.cpp:67).
// glibc reports at the first `free` after the corruption, so the reported
// frame is the DETECTION point, not the cause. Anyone reading a backtrace here
// must not assume the guilty write is inside avformat_close_input.
//
// ── Why the previous revision of this harness could never find it ─────────
//   1. `one_cycle()` constructed a FRESH decoder per cycle, so every Session
//      had exactly one owner and the decoder-wide session map (`m_sessions`,
//      `open_session_locked`) was never mutated concurrently. The topology
//      that actually crashes was therefore never exercised: CASE C/D were
//      running N independent decoders, not one shared decoder with N Sessions.
//   2. `one_cycle()` returned `true` unconditionally and discarded the
//      `decode_frame_at()` result, so the suite reported PASS even if decode
//      produced nothing at all.
//   Both defects are fixed here: ONE decoder is shared by every worker within
//   a cycle, and every decode result is validated (frame present, framebuffer
//   present, dimensions exact, and — under swscale — distinct source pixels).
//
// ── Topology the gate must cover ──────────────────────────────────────────
//
//   for iteration in 0..N:
//       ONE NativeVideoFrameDecoder
//         set_test_options(...)
//         +-----------+-----------+     N threads, N distinct sources,
//         |           |           |     ALL through the SAME decoder
//      source 0    source 1    source k
//         +-----------+-----------+
//                    join
//       destroy the ONE decoder          <-- Session::~Session lives here
//
// ── Bisection matrix (first row that goes green names the guilty subsystem) ─
//   A  shared / prefetch OFF / swscale ON  / cache ON   /  2x2  (repro shape)
//   B  shared / prefetch OFF / swscale OFF / cache ON   /  2x2
//   C  shared / prefetch OFF / swscale OFF / cache OFF  /  2x2
//   D  shared / prefetch OFF / swscale ON  / cache ON   / 64x64 (stride/align)
//   E  shared / prefetch ON  / swscale ON  / cache ON   /  2x2  (production)
//
// ── Gate ──────────────────────────────────────────────────────────────────
// 1000/1000 shared-decoder cycles, every decode validated, and
// 0 OOB / 0 UAF / 0 UB under ASan.
//
// Run (resource budget: the host swaps above ~12 jobs, so stay at or below
// -j12..-j16 and never let several link steps run in parallel):
//   cmake --preset linux-asan-native
//   cmake --build --preset linux-asan-native -j16
//   ctest --preset linux-asan-native-test \
//       -R '^chronon3d_native_decoder_teardown_tests$' --output-on-failure

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/decode_result.hpp>
#include <chronon3d/media/video/native_video_frame_decoder.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

// Y4M C420jpeg writer parameterised by frame size. C420jpeg is 4:2:0 with
// 2x2 chroma subsampling, so a frame is W*H luma bytes followed by two
// (W/2)*(H/2) chroma planes. The 2x2 case (6 bytes) is the fixture shape the
// production crash was observed with; 64x64 widens the stride/alignment
// surface that swscale touches.
void write_y4m(const std::filesystem::path& path, int width, int height,
               std::uint8_t luma, std::uint8_t u, std::uint8_t v) {
    REQUIRE(width > 0);
    REQUIRE(height > 0);
    REQUIRE(width % 2 == 0);
    REQUIRE(height % 2 == 0);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W" << width << " H" << height
           << " F30:1 Ip A1:1 C420jpeg\nFRAME\n";
    REQUIRE(output.good());

    const auto luma_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto chroma_bytes = static_cast<std::size_t>(width / 2) * static_cast<std::size_t>(height / 2);
    std::vector<char> frame(luma_bytes + chroma_bytes * 2);
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < luma_bytes; ++i) frame[cursor++] = static_cast<char>(luma);
    for (std::size_t i = 0; i < chroma_bytes; ++i) frame[cursor++] = static_cast<char>(u);
    for (std::size_t i = 0; i < chroma_bytes; ++i) frame[cursor++] = static_cast<char>(v);
    output.write(frame.data(), static_cast<std::streamsize>(frame.size()));
    REQUIRE(output.good());
}

// Build `count` distinct Y4M sources. Sources 0 and 1 reproduce the exact
// red-biased / blue-biased pair from the production reproducer, so a decoder
// that hands both threads the same decode buffer cannot pass. Remaining
// sources vary per index so an 8-worker row keeps exercising distinct state.
std::vector<std::filesystem::path> make_sources(const FixtureDirectory& fx, int count,
                                                int width, int height) {
    std::vector<std::filesystem::path> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto p = fx.path / ("stress_" + std::to_string(i) + "_" +
                            std::to_string(width) + "x" + std::to_string(height) + ".y4m");
        std::uint8_t luma = 0;
        std::uint8_t u = 0;
        std::uint8_t v = 0;
        if (i == 0) {
            luma = 76; u = 84;  v = 255;
        } else if (i == 1) {
            luma = 29; u = 255; v = 107;
        } else {
            const auto b = static_cast<std::uint8_t>((i * 37 + 5) & 0xff);
            luma = b;
            u = static_cast<std::uint8_t>(255 - b);
            v = static_cast<std::uint8_t>((b * 3) & 0xff);
        }
        write_y4m(p, width, height, luma, u, v);
        out.push_back(std::move(p));
    }
    return out;
}

struct RunConfig {
    int sources{1};
    int threads{1};
    int iterations{1};
    int width{2};
    int height{2};
    chronon3d::media::NativeDecoderTestOptions options;
};

struct CycleCounters {
    int decoded{0};
    int rejected{0};
    int bad_dimensions{0};
    bool pixels_distinct{true};
};

// Run `iterations` cycles. Each cycle builds ONE decoder, runs `threads`
// workers that decode `threads` distinct sources concurrently through that
// single decoder, joins, and then destroys the decoder. The destruction is
// the point: it is where Session::~Session runs after concurrent shared use.
//
// Returns the counters for the caller to assert on, so the suite can never
// report green while decode produced nothing.
CycleCounters run_shared_cycles(const FixtureDirectory& fx, const RunConfig& cfg) {
    REQUIRE(cfg.sources > 0);
    REQUIRE(cfg.threads > 0);
    REQUIRE(cfg.iterations > 0);

    const auto sources = make_sources(fx, cfg.sources, cfg.width, cfg.height);

    CycleCounters counters;
    std::atomic<int> decoded{0};
    std::atomic<int> rejected{0};
    std::atomic<int> bad_dimensions{0};

    // Red channel of pixel (0,0) observed per source. Only the two reference
    // sources are compared; a stale or shared decode buffer makes them equal.
    std::vector<std::atomic<int>> first_red(static_cast<std::size_t>(cfg.sources));
    for (auto& slot : first_red) slot.store(-1, std::memory_order_relaxed);

    for (int iteration = 0; iteration < cfg.iterations; ++iteration) {
        // ONE decoder for the whole cycle, shared by every worker below.
        chronon3d::media::NativeVideoFrameDecoder decoder;
        decoder.set_test_options(cfg.options);

        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(cfg.threads));
        for (int t = 0; t < cfg.threads; ++t) {
            workers.emplace_back([&, t]() {
                const auto idx = static_cast<std::size_t>(
                    (t + iteration) % static_cast<int>(sources.size()));
                const auto result = decoder.decode_frame_at(
                    sources[idx].string(), chronon3d::RationalTime{0, {1, 30}},
                    cfg.width, cfg.height);

                const auto* frame = chronon3d::media::decoded_frame_if(result);
                if (frame == nullptr || frame->framebuffer == nullptr) {
                    rejected.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                if (frame->framebuffer->width() != cfg.width ||
                    frame->framebuffer->height() != cfg.height) {
                    bad_dimensions.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                decoded.fetch_add(1, std::memory_order_relaxed);

                if (cfg.options.enable_swscale) {
                    const auto red = static_cast<int>(
                        frame->framebuffer->get_pixel(0, 0).r * 255.0f + 0.5f);
                    auto& slot = first_red[idx];
                    int expected = -1;
                    slot.compare_exchange_strong(expected, red, std::memory_order_relaxed);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        // The ONE decoder is destroyed here, after concurrent shared use.
        // Before the fix this is where the heap corruption aborted the process.
    }

    counters.decoded = decoded.load(std::memory_order_relaxed);
    counters.rejected = rejected.load(std::memory_order_relaxed);
    counters.bad_dimensions = bad_dimensions.load(std::memory_order_relaxed);

    if (cfg.options.enable_swscale && cfg.sources >= 2) {
        const int a = first_red[0].load(std::memory_order_relaxed);
        const int b = first_red[1].load(std::memory_order_relaxed);
        counters.pixels_distinct = (a >= 0 && b >= 0 && a != b);
    }
    return counters;
}

// Every cycle must have produced a validated frame from every worker, and the
// two reference sources must not have collapsed onto the same pixels.
void check_cycles(const CycleCounters& counters, const RunConfig& cfg) {
    const int expected = cfg.iterations * cfg.threads;
    CHECK(counters.rejected == 0);
    CHECK(counters.bad_dimensions == 0);
    CHECK(counters.decoded == expected);
    if (cfg.options.enable_swscale && cfg.sources >= 2) {
        CHECK(counters.pixels_distinct);
    }
}

} // namespace

// ── Baseline cases: one shared decoder, concurrent session map ────────────
// The matrix rows reproduce the production reproducer shape, whose options
// are prefetch OFF / swscale ON / cache ON. Prefetch is deliberately not the
// first suspect: the crashing test already ran with prefetch disabled, and it
// has its own dedicated concurrency coverage.

[[nodiscard]] chronon3d::media::NativeDecoderTestOptions repro_options() {
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    return opts;
}

TEST_CASE("decoder teardown stress: CASE A — 1 source / 1 thread / 1000 shared cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 1, .threads = 1, .iterations = 1000,
                        .options = repro_options()};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

TEST_CASE("decoder teardown stress: CASE B — 2 sources / sequential / 1000 shared cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 2, .threads = 1, .iterations = 1000,
                        .options = repro_options()};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

// THE reproducer topology: ONE decoder, two threads, two different sources,
// concurrent. If any case in this file goes red, it is expected to be this one.
TEST_CASE("decoder teardown stress: CASE C — 2 sources / 2 threads / 1000 shared cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 1000,
                        .options = repro_options()};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

TEST_CASE("decoder teardown stress: CASE D — 8 sources / 8 threads / 1000 shared cycles") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 8, .threads = 8, .iterations = 1000,
                        .options = repro_options()};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

TEST_CASE("decoder teardown stress: CASE E — 64x64 fixture (stride/alignment)") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 500,
                        .width = 64, .height = 64, .options = repro_options()};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

// ── Bisection matrix (CASE C shape, 200 cycles to keep ASan runtime sane) ──
// Each row disables one subsystem. The first row that passes where the
// previous row crashed names the guilty subsystem.

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale ON / cache ON") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 200, .options = opts};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale OFF / cache ON") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    opts.enable_swscale = false;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 200, .options = opts};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

TEST_CASE("decoder teardown bisection: prefetch OFF / swscale OFF / cache OFF") {
    FixtureDirectory fx;
    chronon3d::media::NativeDecoderTestOptions opts;
    opts.enable_prefetch = false;
    opts.enable_swscale = false;
    opts.enable_frame_cache = false;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 200, .options = opts};
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}

// ── Production shape: prefetch ON (the default path the stress rows skip) ──
// Same shared-decoder topology as CASE C but with the shipping defaults, so
// the prefetch worker thread and the decode path contend on one Session set.

TEST_CASE("decoder teardown stress: production shape / prefetch ON") {
    FixtureDirectory fx;
    const RunConfig cfg{.sources = 2, .threads = 2, .iterations = 200};
    CHECK(cfg.options.enable_prefetch);
    check_cycles(run_shared_cycles(fx, cfg), cfg);
}
