#include <doctest/doctest.h>
#include <chronon3d/runtime/device_scheduler.hpp>

using namespace chronon3d::runtime;

TEST_CASE("DeviceScheduler: empty returns nullopt on reserve") {
    DeviceScheduler scheduler;
    CHECK(scheduler.device_count() == 0);
    auto res = scheduler.reserve(DeviceResourceVector{.compute_units = 1.0f, .vram_bytes = 1024});
    CHECK_FALSE(res.has_value());
}

TEST_CASE("DeviceScheduler: single device basic reservation and RAII release") {
    DeviceScheduler scheduler;
    DeviceCapabilities caps{.name = "RTX 4090", .cuda = true, .nvenc = true};
    DeviceResourceVector cap{.compute_units = 1.0f, .vram_bytes = 24ULL * 1024 * 1024 * 1024, .nvdec_sessions = 4, .nvenc_sessions = 4};
    scheduler.register_device(caps, cap);
    CHECK(scheduler.device_count() == 1);

    {
        auto res = scheduler.reserve(DeviceResourceVector{.compute_units = 0.5f, .vram_bytes = 4ULL * 1024 * 1024 * 1024, .nvenc_sessions = 1});
        REQUIRE(res.has_value());
        CHECK(res->valid());
        CHECK(res->device() == 0);

        auto state = scheduler.resource_state(0);
        REQUIRE(state.has_value());
        CHECK(state->reserved.compute_units == doctest::Approx(0.5f));
        CHECK(state->reserved.vram_bytes == 4ULL * 1024 * 1024 * 1024);
        CHECK(state->reserved.nvenc_sessions == 1);
    }

    // After RAII scope exit, reservation should be automatically released
    auto state = scheduler.resource_state(0);
    REQUIRE(state.has_value());
    CHECK(state->reserved.compute_units == 0.0f);
    CHECK(state->reserved.vram_bytes == 0);
    CHECK(state->reserved.nvenc_sessions == 0);
}

TEST_CASE("DeviceScheduler: multi-device deterministic pressure-based placement") {
    DeviceScheduler scheduler;

    // Device 0: GPU A
    scheduler.register_device(
        DeviceCapabilities{.name = "GPU_0"},
        DeviceResourceVector{.compute_units = 1.0f, .vram_bytes = 1000, .nvenc_sessions = 2});

    // Device 1: GPU B (identical capacity)
    scheduler.register_device(
        DeviceCapabilities{.name = "GPU_1"},
        DeviceResourceVector{.compute_units = 1.0f, .vram_bytes = 1000, .nvenc_sessions = 2});

    // Reserve on least loaded (initially device 0 due to tie-break)
    auto res1 = scheduler.reserve(DeviceResourceVector{.compute_units = 0.5f, .vram_bytes = 500, .nvenc_sessions = 1});
    REQUIRE(res1.has_value());
    CHECK(res1->device() == 0);

    // Next reserve should balance to device 1
    auto res2 = scheduler.reserve(DeviceResourceVector{.compute_units = 0.5f, .vram_bytes = 500, .nvenc_sessions = 1});
    REQUIRE(res2.has_value());
    CHECK(res2->device() == 1);

    // Third reserve exceeds remaining capacity on both for large demand
    auto res_overflow = scheduler.reserve(DeviceResourceVector{.compute_units = 0.8f, .vram_bytes = 800});
    CHECK_FALSE(res_overflow.has_value());
}
