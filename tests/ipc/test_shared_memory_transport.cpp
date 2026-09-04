#include <doctest/doctest.h>

#include "src/ipc/transport/memfd_shared_memory_transport.hpp"

#include <atomic>
#include <thread>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

TEST_CASE("SharedMemoryTransport round-trips bounded wire frames") {
#if defined(__linux__)
    using namespace chronon3d::ipc;
    const std::string address = "test-memfd-thread-" + std::to_string(::getpid());
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
    serving.join();
    CHECK(serve_result.load(std::memory_order_acquire) == 0);
    server.close();
#else
    MESSAGE("SharedMemoryTransport memfd backend is Linux-only");
#endif
}

TEST_CASE("SharedMemoryTransport transfers the memfd across a process boundary") {
#if defined(__linux__)
    using namespace chronon3d::ipc;
    const std::string address = "test-memfd-process-" + std::to_string(::getpid());
    SharedMemoryTransport server;
    server.listen(address);

    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        try {
            SharedMemoryTransport client;
            client.connect(address);
            const WireFrame request{9, 8, 7, 6};
            const auto reply = client.request(request);
            const bool ok = reply.has_value() && *reply == WireFrame{6, 7, 8, 9};
            client.close();
            ::_exit(ok ? 0 : 2);
        } catch (...) {
            ::_exit(3);
        }
    }

    const int serve_result = server.serve([](const WireFrame& request) {
        return WireFrame(request.rbegin(), request.rend());
    });
    CHECK(serve_result == 0);

    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    server.close();
#else
    MESSAGE("SCM_RIGHTS memfd transfer is Linux-only");
#endif
}

TEST_CASE("SharedMemoryTransport rejects empty and oversized frames") {
    using namespace chronon3d::ipc;
    SharedMemoryTransport transport;
    CHECK_FALSE(transport.request({}).has_value());
    CHECK_FALSE(transport.request(WireFrame(SharedMemoryTransport::kMaxFrameLen + 1, 0)).has_value());
}
