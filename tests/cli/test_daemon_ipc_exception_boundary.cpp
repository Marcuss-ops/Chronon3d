#include <doctest/doctest.h>

#include "daemon/chronon_ipc.hpp"

#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#include <unistd.h>

using namespace chronon3d::cli::ipc;

// ---------------------------------------------------------------------------
// TICKET-DAEMON-IPC-REJECTION-RESILIENCE
//
// A typed render rejection (UnsupportedCapability) is a JOB-level result.  It
// must be delivered as a complete error envelope over the same socket, and it
// must not poison the transport, the daemon health, or the next valid request.
// The first case exercises the dispatch boundary directly; the second drives
// the real UnixSocketServer/UnixSocketClient transport to prove the whole
// request/reply/health sequence survives the rejection.
// ---------------------------------------------------------------------------

TEST_CASE("daemon IPC converts a rejected request without unwinding dispatch") {
    const RequestHandler handler = [](const Request& request) -> Reply {
        if (request.cmd == Command::RenderJob) {
            throw std::runtime_error("unsupported animation capability");
        }
        if (request.cmd == Command::Shutdown) {
            return Reply{Status::Shutdown, "bye"};
        }
        return Reply{Status::Ok, "alive"};
    };
    const auto rejected = detail::dispatch_request_safely(
        handler, Request{Command::RenderJob, "{}"});
    CHECK(rejected.status == Status::Error);
    CHECK(rejected.message.find("unsupported animation capability") !=
              std::string::npos);

    const auto status = detail::dispatch_request_safely(
        handler, Request{Command::Status, {}});
    CHECK(status.status == Status::Ok);
    CHECK(status.message == "alive");
}

TEST_CASE("daemon IPC delivers a typed rejection and keeps serving") {
    const auto path = (std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-ipc-exception-" + std::to_string(::getpid()))).string();
    UnixSocketServer server;
    server.listen(path);
    // First render request is the capability rejection; the next one is a
    // valid job, so the transport must still route it to a normal reply.
    std::atomic<int> render_attempts{0};
    const RequestHandler handler = [&render_attempts](const Request& request) -> Reply {
        if (request.cmd == Command::RenderJob) {
            if (render_attempts.fetch_add(1) == 0) {
                throw std::runtime_error("unsupported animation capability");
            }
            return Reply{Status::Ok, "rendered"};
        }
        if (request.cmd == Command::Shutdown) {
            return Reply{Status::Shutdown, "bye"};
        }
        return Reply{Status::Ok, "alive"};
    };
    std::atomic<int> serve_result{-1};
    std::thread serving([&] { serve_result.store(server.serve_concurrent(handler)); });

    UnixSocketClient client;
    client.connect(path);

    // 1. The rejection arrives as a complete typed error envelope.
    const auto rejected = client.request(Command::RenderJob, "{}");
    CHECK(rejected.has_value());
    if (rejected) {
        CHECK(rejected->status == Status::Error);
        CHECK(rejected->message.find("unsupported animation capability") !=
              std::string::npos);
    }

    // 2. The daemon health request over the SAME socket still succeeds.
    const auto status = client.request(Command::Status);
    CHECK(status.has_value());
    if (status) {
        CHECK(status->status == Status::Ok);
        CHECK(status->message == "alive");
    }

    // 3. A subsequent valid job is served normally (transport is not poisoned).
    const auto valid = client.request(Command::RenderJob, "{}");
    CHECK(valid.has_value());
    if (valid) {
        CHECK(valid->status == Status::Ok);
        CHECK(valid->message == "rendered");
    }

    const auto shutdown = client.request(Command::Shutdown);
    CHECK(shutdown.has_value());
    if (shutdown) CHECK(shutdown->status == Status::Shutdown);
    serving.join();
    CHECK(serve_result.load() == 0);
    client.close();
    server.close();
}
