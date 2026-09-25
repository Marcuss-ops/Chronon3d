#include <doctest/doctest.h>

#include "daemon/chronon_ipc.hpp"
#include "daemon/daemon_service.hpp"
#include "commands/render/command_render_plan.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
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

TEST_CASE("daemon socket survives a rejected render job and accepts the next one") {
    using namespace std::chrono_literals;

    const auto suffix = std::to_string(static_cast<long>(::getpid()));
    const auto socket_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-rejection-" + suffix + ".sock");
    const auto plan_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-rejection-" + suffix + ".json");
    const auto output_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-rejection-" + suffix + ".png");
    {
        std::ofstream plan(plan_path);
        REQUIRE(plan.is_open());
        plan << R"({"schema":"chronon.render-plan.v2","version":2,"job_id":"daemon-rejection-test","canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},"layers":[{"id":"bg","type":"color","color":[0,0,0,1],"size":[64,64],"start_frame":0,"duration_frames":1}],"output":{"path":"unused.png","format":"png"}})";
    }

    chronon3d::CompositionRegistry registry;
    chronon3d::cli::DaemonOptions options;
    options.backend = "software";
    chronon3d::cli::DaemonService service(registry, std::move(options));

    auto& dispatcher = chronon3d::cli::render_job_dispatcher();
    auto previous_dispatcher = std::move(dispatcher);
    std::atomic<unsigned> dispatch_count{0};
    std::mutex dispatch_mutex;
    std::condition_variable dispatch_cv;
    bool full_video_dispatch_waiting = false;
    bool allow_full_video_dispatch = false;
    dispatcher = [&dispatch_count, &registry, &dispatch_mutex, &dispatch_cv,
                  &full_video_dispatch_waiting, &allow_full_video_dispatch](
        const chronon3d::runtime::job::NormalizedRenderJob& job,
        std::shared_ptr<chronon3d::SoftwareRenderer> renderer,
        std::shared_ptr<chronon3d::media::VideoJobExecutionContext> video_execution) -> Reply {
        if (dispatch_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            throw std::runtime_error(
                "UnsupportedCapability: deterministic test rejection");
        }
        {
            std::unique_lock lock(dispatch_mutex);
            full_video_dispatch_waiting = true;
            dispatch_cv.notify_all();
            dispatch_cv.wait(lock, [&allow_full_video_dispatch] {
                return allow_full_video_dispatch;
            });
        }
        return chronon3d::cli::ipc_render_job(
            registry, job, std::move(renderer), std::move(video_execution));
    };

    std::thread daemon_thread([&service, &socket_path] {
        service.run_socket(socket_path.string());
    });

    UnixSocketClient client;
    UnixSocketClient status_client;
    bool connected = false;
    for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
        if (std::filesystem::exists(socket_path)) {
            try {
                client.connect(socket_path.string());
                status_client.connect(socket_path.string());
                connected = true;
            } catch (const std::system_error&) {
                // The socket inode may appear just before listen() completes.
            }
        }
        if (!connected) std::this_thread::sleep_for(10ms);
    }

    std::optional<Reply> rejected;
    std::optional<Reply> rejected_status;
    std::optional<Reply> unknown_status;
    std::optional<Reply> health;
    std::optional<Reply> accepted;
    std::optional<Reply> running_status;
    std::optional<Reply> accepted_status;
    std::optional<Reply> duplicate_job;
    std::optional<Reply> shutdown;
    if (connected) {
        const auto make_payload = [&](std::string_view job_id) {
            return std::string{R"({"job_id":")"} + std::string{job_id} +
            R"(","plan_path":")" + plan_path.string() +
            R"(","output":")" + output_path.string() +
            R"(","range_enabled":true,"first_frame":0,"last_frame":0,"output_spec":{"width":64,"height":64,"encoder_backend":"pipe","gpu_hot_path_mode":"auto"},"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":false,"packet_copy_allowed":true}})";
        };
        unknown_status = status_client.request(Command::GetJobStatus,
                                              "no-such-daemon-job");
        rejected = client.request(Command::RenderJob,
                                  make_payload("daemon-rejection-failed"));
        rejected_status = client.request(Command::GetJobStatus,
                                         "daemon-rejection-failed");
        health = client.request(Command::Status);
        std::thread render_thread([&] {
            accepted = client.request(Command::RenderJob,
                                      make_payload("daemon-rejection-completed"));
        });
        bool dispatch_waiting = false;
        {
            std::unique_lock lock(dispatch_mutex);
            dispatch_waiting = dispatch_cv.wait_for(lock, 5s, [&] {
                return full_video_dispatch_waiting;
            });
        }
        if (dispatch_waiting) {
            running_status = status_client.request(
                Command::GetJobStatus, "daemon-rejection-completed");
        }
        {
            std::lock_guard lock(dispatch_mutex);
            allow_full_video_dispatch = true;
        }
        dispatch_cv.notify_all();
        render_thread.join();
        accepted_status = status_client.request(
            Command::GetJobStatus, "daemon-rejection-completed");
        duplicate_job = client.request(
            Command::RenderJob,
            make_payload("daemon-rejection-completed"));
        shutdown = client.request(Command::Shutdown);
    }

    client.close();
    status_client.close();
    if (daemon_thread.joinable()) daemon_thread.join();
    const bool output_created = std::filesystem::exists(output_path);
    dispatcher = std::move(previous_dispatcher);
    std::error_code ignored;
    std::filesystem::remove(plan_path, ignored);
    std::filesystem::remove(output_path, ignored);
    std::filesystem::remove(socket_path, ignored);

    REQUIRE(connected);
    REQUIRE(rejected.has_value());
    CHECK(rejected->status == Status::Error);
    CHECK(rejected->message.find("UnsupportedCapability") != std::string::npos);
    REQUIRE(rejected_status.has_value());
    CHECK(rejected_status->status == Status::Ok);
    CHECK(rejected_status->message.find("\"state\":\"failed\"") != std::string::npos);
    REQUIRE(unknown_status.has_value());
    CHECK(unknown_status->status == Status::NotFound);
    REQUIRE(health.has_value());
    CHECK(health->status == Status::Ok);
    CHECK(health->message.find("frames_rendered=") != std::string::npos);
    REQUIRE(accepted.has_value());
    CHECK(accepted->status == Status::Ok);
    CHECK(output_created);
    CHECK(accepted->message.find("\"status\":\"ok\"") != std::string::npos);
    REQUIRE(running_status.has_value());
    CHECK(running_status->status == Status::Ok);
    CHECK(running_status->message.find("\"state\":\"running\"") != std::string::npos);
    REQUIRE(accepted_status.has_value());
    CHECK(accepted_status->status == Status::Ok);
    CHECK(accepted_status->message.find("\"state\":\"completed\"") != std::string::npos);
    REQUIRE(duplicate_job.has_value());
    CHECK(duplicate_job->status == Status::BadRequest);
    CHECK(dispatch_count.load(std::memory_order_relaxed) == 2);
    REQUIRE(shutdown.has_value());
    CHECK(shutdown->status == Status::Shutdown);
    CHECK(dispatch_count.load(std::memory_order_relaxed) == 2);
}
