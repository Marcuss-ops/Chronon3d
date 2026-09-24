#include <doctest/doctest.h>

#include "daemon/chronon_ipc.hpp"

#include <stdexcept>

using namespace chronon3d::cli::ipc;

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
