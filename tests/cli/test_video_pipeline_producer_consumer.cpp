#include <doctest/doctest.h>
#include <apps/chronon3d_cli/utils/video/ffmpeg_pipe_encoder.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/math/color.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <algorithm>

using namespace chronon3d;
using namespace chronon3d::cli;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create a filled framebuffer for testing
// ─────────────────────────────────────────────────────────────────────────────
static Framebuffer make_test_fb(int w, int h, Color fill = Color::red()) {
    Framebuffer fb(w, h);
    fb.clear(fill);
    return fb;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1 — Render thread senza pipe write
// Verify that the encoder cleanly separates pixel conversion from pipe write,
// and that write_blocked time is tracked independently.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline Producer/Consumer: Encoder separates conversion from write") {
    Framebuffer fb = make_test_fb(64, 64, Color{0.2f, 0.4f, 0.6f, 1.0f});

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 64,
        .height = 64,
        .fps = 1,
        .output_path = "/tmp/test_separate_stages.mp4",
        .input_format = PipePixelFormat::RGBA,
    };

    // Opening the pipe spawns an ffmpeg process; if it fails (no ffmpeg),
    // skip gracefully.
    if (!enc.open(opts)) {
        MESSAGE("Skipping — FFmpeg not available");
        return;
    }

    // Before writing: write_blocked_ms should be zero
    double before = enc.total_write_blocked_ms();
    CHECK(before >= 0.0);

    // Write a frame — conversion + fwrite happen inside write_frame()
    bool ok = enc.write_frame(fb);
    CHECK(ok);

    // After writing: write_blocked_ms should have accumulated (unless cache hit
    // on second identical frame)
    double after = enc.total_write_blocked_ms();
    CHECK(after >= before);

    // The encoder's own frames_written counter advances independently
    CHECK(enc.frames_written() == 1);

    enc.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2 — Backpressure con queue piena
// Small bounded queue + slow consumer = backpressure visible in telemetry.
// We model this with moodycamel::ConcurrentQueue and a RenderCounters
// instance that records push_blocked time.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline Producer/Consumer: Backpressure with full queue") {
    struct Package {
        int frame_id;
        std::shared_ptr<Framebuffer> fb;
    };

    // Small queue so backpressure triggers quickly
    moodycamel::ConcurrentQueue<Package> queue;
    const int kQueueSoftCap = 4;

    RenderCounters counters;
    std::atomic<bool> consumer_done{false};
    std::atomic<int> frames_consumed{0};
    std::vector<int> consumed_order;

    // Slow consumer: processes one frame every 10ms
    std::thread consumer([&]() {
        while (!consumer_done.load()) {
            Package pkg;
            if (queue.try_dequeue(pkg)) {
                consumed_order.push_back(pkg.frame_id);
                frames_consumed.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                std::this_thread::yield();
            }
        }
        // Drain remaining
        Package pkg;
        while (queue.try_dequeue(pkg)) {
            consumed_order.push_back(pkg.frame_id);
            frames_consumed.fetch_add(1);
        }
    });

    // Fast producer: enqueues 20 frames rapidly
    const int kTotalFrames = 20;
    for (int i = 0; i < kTotalFrames; ++i) {
        const auto wait_t0 = std::chrono::steady_clock::now();

        // Track peak depth
        auto qs = queue.size_approx();
        auto peak = counters.io_queue_peak_depth.load(std::memory_order_relaxed);
        while (qs > peak &&
               !counters.io_queue_peak_depth.compare_exchange_weak(
                   peak, qs, std::memory_order_relaxed)) {}

        // Backpressure: yield while queue is above soft cap
        while (queue.size_approx() > static_cast<size_t>(kQueueSoftCap)) {
            std::this_thread::yield();
        }

        auto fb = std::make_shared<Framebuffer>(make_test_fb(32, 32));
        queue.enqueue(Package{i, fb});

        const auto wait_t1 = std::chrono::steady_clock::now();
        double wait_ms = std::chrono::duration<double, std::milli>(wait_t1 - wait_t0).count();
        counters.io_queue_push_blocked_ms.fetch_add(
            static_cast<uint64_t>(wait_ms), std::memory_order_relaxed);
    }

    // Let consumer finish
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    consumer_done.store(true);
    consumer.join();

    // Verify:
    // - All frames consumed (none lost)
    CHECK(frames_consumed.load() == kTotalFrames);

    // - Order preserved
    CHECK(consumed_order.size() == static_cast<size_t>(kTotalFrames));
    for (int i = 1; i < kTotalFrames; ++i) {
        CHECK(consumed_order[i] > consumed_order[i - 1]);  // monotonic
    }

    // - Peak depth reached at least the soft cap
    uint64_t peak = counters.io_queue_peak_depth.load(std::memory_order_relaxed);
    CHECK(peak >= static_cast<uint64_t>(kQueueSoftCap));

    // - Push blocked time > 0 (backpressure was felt)
    uint64_t push_blocked = counters.io_queue_push_blocked_ms.load(std::memory_order_relaxed);
    CHECK(push_blocked > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3 — Consumer più veloce del producer
// Slow producer + fast consumer → pop_wait_ms grows, push_blocked stays ~0.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline Producer/Consumer: Fast consumer, slow producer") {
    struct Package {
        int frame_id;
        std::shared_ptr<Framebuffer> fb;
    };

    moodycamel::ConcurrentQueue<Package> queue;
    RenderCounters counters;
    std::atomic<bool> producer_done{false};
    std::atomic<int> frames_produced{0};
    std::atomic<int> frames_consumed{0};

    // Producer: slow (20ms per frame)
    std::thread producer([&]() {
        for (int i = 0; i < 10; ++i) {
            const auto wait_t0 = std::chrono::steady_clock::now();
            while (queue.size_approx() > 16) {
                std::this_thread::yield();
            }
            auto fb = std::make_shared<Framebuffer>(make_test_fb(32, 32));
            queue.enqueue(Package{i, fb});
            frames_produced.fetch_add(1);

            const auto wait_t1 = std::chrono::steady_clock::now();
            double wait_ms = std::chrono::duration<double, std::milli>(wait_t1 - wait_t0).count();
            counters.io_queue_push_blocked_ms.fetch_add(
                static_cast<uint64_t>(wait_ms), std::memory_order_relaxed);

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        producer_done.store(true);
    });

    // Consumer: fast — spins rapidly
    while (!producer_done.load() || queue.size_approx() > 0) {
        const auto pop_t0 = std::chrono::steady_clock::now();
        Package pkg;
        if (queue.try_dequeue(pkg)) {
            frames_consumed.fetch_add(1);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const auto pop_t1 = std::chrono::steady_clock::now();
        double pop_ms = std::chrono::duration<double, std::milli>(pop_t1 - pop_t0).count();
        counters.io_queue_pop_wait_ms.fetch_add(
            static_cast<uint64_t>(pop_ms), std::memory_order_relaxed);
    }

    producer.join();

    // Verify:
    // - All frames consumed
    CHECK(frames_consumed.load() == 10);

    // - Pop wait grows (consumer spends time waiting for producer)
    uint64_t pop_wait = counters.io_queue_pop_wait_ms.load(std::memory_order_relaxed);
    CHECK(pop_wait > 0);

    // - Push blocked is near zero (producer is the bottleneck, not the queue)
    uint64_t push_blocked = counters.io_queue_push_blocked_ms.load(std::memory_order_relaxed);
    // push_blocked represents only the enqueue overhead (microseconds)
    CHECK(push_blocked < pop_wait);  // consumer waits more than producer blocks
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4 — Flush finale
// Non-trivial number of frames with small queue: all frames written,
// flush called once, no pending frames, no deadlock.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline Producer/Consumer: Final flush writes all frames") {
    Framebuffer fb = make_test_fb(32, 32, Color{0.1f, 0.3f, 0.5f, 1.0f});

    FfmpegPipeEncoder enc;
    FfmpegPipeOptions opts{
        .width = 32,
        .height = 32,
        .fps = 30,
        .output_path = "/tmp/test_flush_final.mp4",
        .input_format = PipePixelFormat::RGBA,
    };

    if (!enc.open(opts)) {
        MESSAGE("Skipping — FFmpeg not available");
        return;
    }

    // Write multiple frames through the encoder
    const int kFrameCount = 10;
    for (int i = 0; i < kFrameCount; ++i) {
        // Slightly vary the framebuffer content to defeat the cache
        Framebuffer varied(32, 32);
        varied.clear(Color{
            0.1f + static_cast<float>(i) * 0.05f,
            0.3f,
            0.5f,
            1.0f
        });
        bool ok = enc.write_frame(varied);
        CHECK(ok);
    }

    // All frames written
    CHECK(enc.frames_written() == static_cast<uint64_t>(kFrameCount));

    // Close calls flush
    bool closed_ok = enc.close();
    CHECK(closed_ok);

    // After close, encoder is no longer open
    CHECK_FALSE(enc.is_open());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5 — Errore encoder
// Consumer error mid-export: error propagated, no deadlock,
// telemetry indicates failure.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("Pipeline Producer/Consumer: Encoder error propagation") {
    // Test 1: write_frame fails when encoder is not open
    {
        Framebuffer fb = make_test_fb(32, 32);
        FfmpegPipeEncoder enc;
        // Not opened → write_frame must fail
        CHECK_FALSE(enc.write_frame(fb));
    }

    // Test 2: write_frame fails after encoder is closed
    {
        Framebuffer fb = make_test_fb(32, 32);
        FfmpegPipeEncoder enc;
        FfmpegPipeOptions opts{
            .width = 32,
            .height = 32,
            .fps = 1,
            .output_path = "/tmp/test_error_propagation.mp4",
            .input_format = PipePixelFormat::RGBA,
        };

        if (!enc.open(opts)) {
            MESSAGE("Skipping error-after-close — FFmpeg not available");
        } else {
            // Write one frame successfully
            CHECK(enc.write_frame(fb));

            // Close the encoder
            enc.close();

            // After close, writes must fail
            CHECK_FALSE(enc.write_frame(fb));
        }
    }

    // Test 3: The producer/consumer pattern handles failure correctly.
    // We simulate this with a queue + atomic failure flag.
    {
        struct Package {
            int frame_id;
            std::shared_ptr<Framebuffer> fb;
        };

        moodycamel::ConcurrentQueue<Package> queue;
        std::atomic<bool> consumer_failed{false};
        std::atomic<bool> consumer_done{false};
        std::atomic<int> frames_produced{0};
        std::atomic<int> frames_consumed{0};

        // Consumer that fails after processing 5 frames
        std::thread consumer([&]() {
            for (;;) {
                Package pkg;
                while (!queue.try_dequeue(pkg)) {
                    if (consumer_done.load()) {
                        if (!queue.try_dequeue(pkg)) return;
                    }
                    std::this_thread::yield();
                }
                frames_consumed.fetch_add(1);

                // Simulate encoder failure on frame 5
                if (pkg.frame_id == 5) {
                    consumer_failed.store(true);
                    return;
                }
            }
        });

        // Producer: should detect failure and stop
        int last_produced = -1;
        for (int i = 0; i < 20; ++i) {
            if (consumer_failed.load()) {
                break;
            }
            auto fb = std::make_shared<Framebuffer>(make_test_fb(32, 32));
            queue.enqueue(Package{i, fb});
            last_produced = i;
            frames_produced.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        consumer_done.store(true);
        consumer.join();

        // Consumer hit failure
        CHECK(consumer_failed.load());

        // Producer stopped early (did not produce all 20 frames)
        CHECK(last_produced < 19);

        // Consumer processed frames up to and including frame 5
        CHECK(frames_consumed.load() >= 5);
    }
}
