#include <doctest/doctest.h>

#include "src/ipc/shared_memory_transport.hpp"

#include <atomic>
#include <thread>

TEST_CASE("SharedMemoryTransport round-trips bounded wire frames") {
    using namespace chronon3d::ipc;
    const std::string address = "test-shm-" + std::to_string(::getpid());
    SharedMemoryTransport server;
    server.listen(address);
    std::atomic<int> serve_result{-1};
    std::thread serving([&] {
        serve_result.store(server.serve([](const WireFrame& request) {
            return WireFrame(request.rbegin(), request.rend());
        }), std::memory_order_release);
    });

    SharedMemoryTransport client;
    client.connect(address);
    const WireFrame request{1, 2, 3, 4};
    const auto reply = client.request(request);
    REQUIRE(reply.has_value());
    CHECK(*reply == WireFrame{4, 3, 2, 1});
    client.close();
    server.close();
    serving.join();
    CHECK(serve_result.load(std::memory_order_acquire) == 0);
}

TEST_CASE("SharedMemoryTransport rejects empty and oversized frames") {
    using namespace chronon3d::ipc;
    SharedMemoryTransport transport;
    CHECK_FALSE(transport.request({}).has_value());
    CHECK_FALSE(transport.request(WireFrame(SharedMemoryTransport::kMaxFrameLen + 1, 0)).has_value());
}
