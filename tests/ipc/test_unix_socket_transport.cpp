// Regression tests for the Unix-domain IPC transport contract (P0 shutdown,
// P1 head-of-line blocking):
//
//  1. A handler returning ServerReply{..., shutdown=true} must terminate
//     serve() (the daemon process then exits) AFTER the acknowledgment reply
//     is delivered — never keep accepting new clients.
//  2. An idle client holding its socket open must not block other clients
//     from being served (connections are served concurrently).

#include <doctest/doctest.h>

#include "src/ipc/unix_socket_transport.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

using namespace chronon3d::ipc;

namespace {

std::string socket_path(const char* tag) {
    return (std::filesystem::temp_directory_path() /
            ("chronon3d-ipc-test-" + std::string(tag) + "-" +
             std::to_string(static_cast<long>(::getpid())))).string();
}

// Reverses every frame; a single-frame request {'S'} is the Shutdown signal.
FrameHandler echo_or_shutdown() {
    return [](const WireFrame& request) -> ServerReply {
        if (request == WireFrame{'S'}) {
            return ServerReply{WireFrame{'b', 'y', 'e'}, true};
        }
        return ServerReply{WireFrame(request.rbegin(), request.rend()), false};
    };
}

bool request_roundtrip(UnixSocketClientTransport& client, const WireFrame& in,
                       WireFrame* out) {
    const auto reply = client.request(in);
    if (!reply) return false;
    *out = *reply;
    return true;
}

} // namespace

TEST_CASE("UnixSocketTransport stops serving after a Shutdown reply") {
    const std::string path = socket_path("shutdown");
    UnixSocketTransport server;
    server.listen(path);
    std::atomic<int> serve_result{-1};
    std::thread serving([&] { serve_result.store(server.serve(echo_or_shutdown())); });

    UnixSocketClientTransport client;
    client.connect(path);

    WireFrame reply;
    REQUIRE(request_roundtrip(client, {1, 2, 3}, &reply));
    CHECK(reply == WireFrame{3, 2, 1});

    // Shutdown: the acknowledgment must be delivered and serve() must return
    // 0 (the daemon exits) instead of continuing to accept.
    REQUIRE(request_roundtrip(client, {'S'}, &reply));
    CHECK(reply == WireFrame{'b', 'y', 'e'});

    serving.join();
    CHECK(serve_result.load() == 0);

    client.close();
    server.close();
}

TEST_CASE("UnixSocketTransport serves a second client while another stays idle") {
    const std::string path = socket_path("concurrent");
    UnixSocketTransport server;
    server.listen(path);
    std::atomic<int> serve_result{-1};
    std::thread serving([&] { serve_result.store(server.serve(echo_or_shutdown())); });

    // Client A connects and stays idle (the historical single-thread loop
    // would be stuck in serve_client(A) here, blocking everyone else).
    UnixSocketClientTransport idle_client;
    idle_client.connect(path);

    // Client B connects afterwards and must complete its request promptly.
    UnixSocketClientTransport busy_client;
    busy_client.connect(path);
    WireFrame reply;
    const auto started = std::chrono::steady_clock::now();
    REQUIRE(request_roundtrip(busy_client, {7, 8, 9}, &reply));
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    CHECK(reply == WireFrame{9, 8, 7});
    CHECK(elapsed_ms < 4000);

    // Clean shutdown through the busy client terminates serve().
    REQUIRE(request_roundtrip(busy_client, {'S'}, &reply));
    serving.join();
    CHECK(serve_result.load() == 0);

    idle_client.close();
    busy_client.close();
    server.close();
}
