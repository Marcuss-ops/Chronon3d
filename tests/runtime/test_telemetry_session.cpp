#include <doctest/doctest.h>

#include <chronon3d/core/profiling/counters.hpp>
#include <chronon3d/runtime/telemetry/telemetry_session.hpp>

#include <thread>
#include <vector>

using namespace chronon3d;
using namespace chronon3d::telemetry;

TEST_CASE("TelemetrySession: begin_job preallocates frame slots with frame numbers") {
    TelemetrySession session;
    session.begin_job("comp_a", "/tmp/out.mp4", 10);

    CHECK(session.run().composition_id == "comp_a");
    CHECK(session.run().output_path == "/tmp/out.mp4");
    CHECK(session.run().frames_total == 10);

    CHECK(session.frames().size() == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(session.frame(i).frame_number == i);
    }
}

TEST_CASE("TelemetrySession: render and encoder threads write disjoint fields of one slot") {
    TelemetrySession session;
    session.preallocate_frames(4);

    // Render thread owns render-ish fields; encoder thread owns encoder-ish
    // fields.  They write the SAME slot index without a lock and without a
    // final merge pass keyed on frame number.
    std::thread render([&] {
        for (int i = 0; i < 4; ++i) {
            session.frame(i).duration_ms = 16.6 + i;
            session.frame(i).cache_hit = true;
        }
    });

    std::thread encoder([&] {
        for (int i = 0; i < 4; ++i) {
            session.frame(i).encoder_ms = 1.0 + i;
            session.frame(i).pipe_write_ms = 0.5 + i;
        }
    });

    render.join();
    encoder.join();

    for (int i = 0; i < 4; ++i) {
        const auto& f = session.frame(i);
        CHECK(f.duration_ms == doctest::Approx(16.6 + i));
        CHECK(f.cache_hit);
        CHECK(f.encoder_ms == doctest::Approx(1.0 + i));
        CHECK(f.pipe_write_ms == doctest::Approx(0.5 + i));
    }
}

TEST_CASE("TelemetrySession: frame() grows on demand when not preallocated") {
    TelemetrySession session;

    session.frame(5).duration_ms = 42.0;

    CHECK(session.frames().size() == 6);
    CHECK(session.frame(5).frame_number == 5);
    CHECK(session.frame(5).duration_ms == doctest::Approx(42.0));
}

TEST_CASE("TelemetrySession: snapshot_counters copies atomic counters to a stable snapshot") {
    RenderCounters counters;
    counters.cache_hits.store(7, std::memory_order_relaxed);
    counters.cache_misses.store(3, std::memory_order_relaxed);
    counters.pixels_touched.store(123456, std::memory_order_relaxed);
    counters.increment_dirty_full_fallback_reason(DirtyFallbackReason::EffectBoundsUnknown);

    TelemetrySession session;
    session.snapshot_counters(counters);

    const auto& snap = session.counters_snapshot();
    CHECK(snap.cache_hits == 7);
    CHECK(snap.cache_misses == 3);
    CHECK(snap.pixels_touched == 123456);
    CHECK(snap.dirty_full_fallback_reasons[
        static_cast<std::size_t>(DirtyFallbackReason::EffectBoundsUnknown)] == 1);

    // Mutating the source atomics after the snapshot must not change it.
    counters.cache_hits.store(999, std::memory_order_relaxed);
    CHECK(session.counters_snapshot().cache_hits == 7);
}

TEST_CASE("TelemetrySession: detail events reuse the existing record types") {
    TelemetrySession session;

    session.record_node({.node_name = "node_a", .duration_ms = 1.0});
    session.record_node({.node_name = "node_b", .duration_ms = 2.0});
    session.record_layer({.layer_id = "bg", .layer_name = "Background"});
    session.record_cache({.node_name = "node_a", .cache_status = "hit"});
    session.record_culling({.layer_id = "bg", .visible = false});
    session.record_text({.glyph_count = 12});
    session.record_image({.image_path = "a.png"});
    session.record_tile({.tile_status = "hit"});

    CHECK(session.node_events().size() == 2);
    CHECK(session.node_events()[1].node_name == "node_b");
    CHECK(session.layer_events().size() == 1);
    CHECK(session.layer_events()[0].layer_id == "bg");
    CHECK(session.cache_events().size() == 1);
    CHECK(session.culling_events().size() == 1);
    CHECK(session.text_events().size() == 1);
    CHECK(session.image_events().size() == 1);
    CHECK(session.tile_events().size() == 1);
}

TEST_CASE("TelemetrySession: each session is an isolated collector (no global state)") {
    TelemetrySession a;
    TelemetrySession b;

    a.begin_job("comp_a", "/tmp/a.mp4", 3);
    b.begin_job("comp_b", "/tmp/b.mp4", 5);

    a.frame(0).duration_ms = 1.0;
    b.frame(0).duration_ms = 99.0;

    // Mutating one session never leaks into the other.
    CHECK(a.frame(0).duration_ms == doctest::Approx(1.0));
    CHECK(b.frame(0).duration_ms == doctest::Approx(99.0));
    CHECK(a.frames().size() == 3);
    CHECK(b.frames().size() == 5);
    CHECK(a.run().composition_id == "comp_a");
    CHECK(b.run().composition_id == "comp_b");
}

TEST_CASE("TelemetrySession: movable so a job can hand it to the sink") {
    TelemetrySession session;
    session.begin_job("comp_m", "/tmp/m.mp4", 2);
    session.frame(1).encoder_ms = 3.5;

    TelemetrySession moved = std::move(session);

    CHECK(moved.run().composition_id == "comp_m");
    CHECK(moved.frames().size() == 2);
    CHECK(moved.frame(1).encoder_ms == doctest::Approx(3.5));
}

TEST_CASE("TelemetrySession: frame_timing_summary computes first/mean/p95/p99 deterministically") {
    TelemetrySession session;
    session.begin_job("comp_s", "/tmp/s.mp4", 10);
    for (int i = 0; i < 10; ++i) {
        session.frame(i).duration_ms = static_cast<double>(i + 1);  // 1..10 ms
    }

    const auto s = session.frame_timing_summary();

    CHECK(s.first_frame_ms == doctest::Approx(1.0));
    CHECK(s.mean_frame_ms == doctest::Approx(5.5));
    CHECK(s.min_frame_ms == doctest::Approx(1.0));
    CHECK(s.max_frame_ms == doctest::Approx(10.0));
    CHECK(s.p50_frame_ms == doctest::Approx(5.0));
    CHECK(s.p95_frame_ms == doctest::Approx(9.0));
    CHECK(s.p99_frame_ms == doctest::Approx(9.0));
    // 10 frames → 5-frame warmup window; steady state covers the rest.
    CHECK(s.warmup_frames == 5);
    CHECK(s.warmup_avg_ms == doctest::Approx(3.0));
    CHECK(s.steady_avg_ms == doctest::Approx(8.0));
    CHECK(s.steady_p95_ms == doctest::Approx(9.0));
}

TEST_CASE("TelemetrySession: summarize_frame_timings disables the warmup window for short renders") {
    std::vector<FrameTelemetry> frames;
    for (int i = 0; i < 4; ++i) {
        FrameTelemetry f;
        f.frame_number = i;
        f.duration_ms = static_cast<double>(10 * (i + 1));  // 10,20,30,40
        frames.push_back(f);
    }

    const auto s = summarize_frame_timings(frames);

    CHECK(s.warmup_frames == 0);
    CHECK(s.first_frame_ms == doctest::Approx(10.0));
    CHECK(s.mean_frame_ms == doctest::Approx(25.0));
    CHECK(s.p95_frame_ms == doctest::Approx(30.0));
    // With no warmup window the steady stats cover every frame.
    CHECK(s.steady_avg_ms == doctest::Approx(25.0));
}

TEST_CASE("TelemetrySession: summarize_frame_timings returns zeros for empty input") {
    const auto s = summarize_frame_timings({});
    CHECK(s.first_frame_ms == 0.0);
    CHECK(s.mean_frame_ms == 0.0);
    CHECK(s.warmup_frames == 0);
}
