#include <doctest/doctest.h>

#include <chronon3d/media/video/video_device_runtime.hpp>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using chronon3d::media::VideoRuntimeRegistry;

TEST_CASE("VideoRuntimeRegistry: reuses one runtime per device") {
    VideoRuntimeRegistry registry;

    auto first = registry.get_or_create(0);
    auto second = registry.get_or_create(0);
    auto other = registry.get_or_create(1);

    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(other);
    CHECK(first == second);
    CHECK(first != other);
    CHECK(first->device_id() == 0);
    CHECK(other->device_id() == 1);
    CHECK(registry.size() == 2);
}

TEST_CASE("VideoDeviceRuntime: context mismatch fails closed") {
    auto gpu = std::make_shared<chronon3d::runtime::GpuRuntime>();
    std::string reason;
    auto runtime = chronon3d::media::VideoDeviceRuntime::create(0, gpu, reason);
    REQUIRE(runtime);

    CHECK_FALSE(runtime->context_matches(0));
    CHECK_FALSE(runtime->context_matches(0x1));
}

TEST_CASE("VideoRuntimeRegistry: concurrent creation is deduplicated") {
    VideoRuntimeRegistry registry;
    std::vector<std::shared_ptr<chronon3d::media::VideoDeviceRuntime>> results(16);
    std::vector<std::thread> workers;
    workers.reserve(results.size());

    for (auto& result : results) {
        workers.emplace_back([&registry, &result] {
            result = registry.get_or_create(0);
        });
    }
    for (auto& worker : workers) worker.join();

    REQUIRE(results.front());
    for (const auto& result : results) {
        REQUIRE(result);
        CHECK(result == results.front());
    }
    CHECK(registry.size() == 1);
}
