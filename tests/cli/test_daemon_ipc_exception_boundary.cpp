#include <doctest/doctest.h>

#include "daemon/chronon_ipc.hpp"
#include "daemon/daemon_service.hpp"
#include "commands/render/command_render_plan.hpp"
#include <chronon3d/api/render_engine.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

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

// Portability: getpid via <process.h> on Windows (convention: see
// tests/video/test_native_video_retained_replay.cpp).
#ifdef _WIN32
#  include <process.h>
#  define CHRONON3D_GETPID _getpid
#else
#  include <unistd.h>
#  define CHRONON3D_GETPID getpid
#endif

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
        ("chronon3d-daemon-ipc-exception-" + std::to_string(CHRONON3D_GETPID()))).string();
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

    const auto suffix = std::to_string(static_cast<long>(CHRONON3D_GETPID()));
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

TEST_CASE("daemon RENDER_BATCH preserves segment order and isolates a failed segment retry") {
    using namespace std::chrono_literals;

    const auto suffix = std::to_string(static_cast<long>(CHRONON3D_GETPID()));
    const auto socket_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-batch-retry-" + suffix + ".sock");
    const auto plan_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-batch-retry-" + suffix + ".json");
    {
        std::ofstream plan(plan_path);
        REQUIRE(plan.is_open());
        plan << R"({"schema":"chronon.render-plan.v2","version":2,"job_id":"daemon-batch-retry-test","canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},"layers":[{"id":"bg","type":"color","color":[0,0,0,1],"size":[64,64],"start_frame":0,"duration_frames":1}],"output":{"path":"unused.png","format":"png"}})";
    }

    chronon3d::CompositionRegistry registry;
    chronon3d::cli::DaemonOptions options;
    options.backend = "software";
    chronon3d::cli::DaemonService service(registry, std::move(options));

    auto& dispatcher = chronon3d::cli::render_job_dispatcher();
    auto previous_dispatcher = std::move(dispatcher);
    std::atomic<unsigned> dispatch_count{0};
    std::vector<std::string> dispatched_outputs;
    dispatcher = [&dispatch_count, &dispatched_outputs](
        const chronon3d::runtime::job::NormalizedRenderJob& job,
        std::shared_ptr<chronon3d::SoftwareRenderer>,
        std::shared_ptr<chronon3d::media::VideoJobExecutionContext>) -> Reply {
        const auto call = dispatch_count.fetch_add(1, std::memory_order_relaxed);
        const auto output = job.output_path.string();
        dispatched_outputs.push_back(output);
        if (output == "segment-0.png") {
            return Reply{Status::Error, "segment-0 transient failure"};
        }
        return Reply{Status::Ok, "segment render complete"};
    };

    std::thread daemon_thread([&service, &socket_path] {
        service.run_socket(socket_path.string());
    });
    UnixSocketClient client;
    bool connected = false;
    for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
        if (std::filesystem::exists(socket_path)) {
            try {
                client.connect(socket_path.string());
                connected = true;
            } catch (const std::system_error&) {
                // The socket inode can exist before listen() is ready.
            }
        }
        if (!connected) std::this_thread::sleep_for(10ms);
    }

    const auto make_payload = [&](std::string_view job_id,
                                  std::string_view output) {
        return std::string{R"({"job_id":")"} + std::string{job_id} +
            R"(","plan_path":")" + plan_path.string() +
            R"(","output":")" + std::string{output} +
            R"(","backend":"software","range_enabled":true,"first_frame":0,"last_frame":0,"output_spec":{"width":64,"height":64,"encoder_backend":"pipe","gpu_hot_path_mode":"auto"},"execution_requirements":{"backend":"software","gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":false,"packet_copy_allowed":true}})";
    };

    std::optional<Reply> batch_reply;
    std::optional<Reply> retry_reply;
    std::optional<Reply> shutdown;
    if (connected) {
        const auto first = make_payload("batch-segment-0", "segment-0.png");
        const auto second = make_payload("batch-segment-1", "segment-1.png");
        const std::string batch_payload =
            std::string{"{\"jobs\":"} + "[" + first + "," + second + "]}";
        batch_reply = client.request(Command::RenderBatch, batch_payload);
        retry_reply = client.request(
            Command::RenderJob,
            make_payload("retry-segment-0", "segment-0-retry.png"));
        shutdown = client.request(Command::Shutdown);
    }

    client.close();
    if (daemon_thread.joinable()) daemon_thread.join();
    dispatcher = std::move(previous_dispatcher);
    std::error_code ignored;
    std::filesystem::remove(plan_path, ignored);
    std::filesystem::remove(socket_path, ignored);

    REQUIRE(connected);
    REQUIRE(batch_reply.has_value());
    CHECK(batch_reply->status == Status::Error);
    const auto batch_result = nlohmann::json::parse(batch_reply->message);
    REQUIRE(batch_result.contains("results"));
    REQUIRE(batch_result["results"].size() == 2);
    CHECK(batch_result["results"][0]["index"] == 0);
    CHECK(batch_result["results"][0]["status"] == "error");
    CHECK(batch_result["results"][0]["message"] == "segment-0 transient failure");
    CHECK(batch_result["results"][1]["index"] == 1);
    CHECK(batch_result["results"][1]["status"] == "ok");
    REQUIRE(dispatched_outputs.size() == 3);
    CHECK(dispatched_outputs[0] == "segment-0.png");
    CHECK(dispatched_outputs[1] == "segment-1.png");
    CHECK(dispatched_outputs[2] == "segment-0-retry.png");
    REQUIRE(retry_reply.has_value());
    CHECK(retry_reply->status == Status::Ok);
    CHECK(dispatch_count.load(std::memory_order_relaxed) == 3);
    REQUIRE(shutdown.has_value());
    CHECK(shutdown->status == Status::Shutdown);
}

TEST_CASE("daemon runs the deferred receipt step after releasing the render lane") {
    using namespace std::chrono_literals;

    // -----------------------------------------------------------------------
    // TICKET-OVERLAY-THROUGHPUT fix 1
    //
    // The receipt step (probe + sha256 + full-decode gate) verifies an
    // already-finished render: it is NOT render work. The daemon must release
    // the execution lock and the device reservation BEFORE running it, so the
    // next RENDER_JOB is admitted and dispatched while the previous job's
    // receipt is still being verified. The reply of the deferred job still
    // leaves only after the step completed, because the worker reads the
    // receipt file right after the reply.
    // -----------------------------------------------------------------------
    const auto suffix = std::to_string(static_cast<long>(CHRONON3D_GETPID()));
    const auto socket_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-receipt-deferral-" + suffix + ".sock");
    const auto plan_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-receipt-deferral-" + suffix + ".json");
    const auto output_path = std::filesystem::temp_directory_path() /
        ("chronon3d-daemon-receipt-deferral-" + suffix + ".png");
    {
        std::ofstream plan(plan_path);
        REQUIRE(plan.is_open());
        plan << R"({"schema":"chronon.render-plan.v2","version":2,"job_id":"daemon-receipt-deferral-test","canvas":{"width":64,"height":64,"fps_num":30,"fps_den":1,"duration_frames":1},"layers":[{"id":"bg","type":"color","color":[0,0,0,1],"size":[64,64],"start_frame":0,"duration_frames":1}],"output":{"path":"unused.png","format":"png"}})";
    }

    chronon3d::CompositionRegistry registry;
    chronon3d::cli::DaemonOptions options;
    options.backend = "software";
    chronon3d::cli::DaemonService service(registry, std::move(options));

    auto& dispatcher = chronon3d::cli::render_job_dispatcher();
    auto previous_dispatcher = std::move(dispatcher);
    std::atomic<unsigned> dispatch_count{0};
    std::mutex state_mutex;
    std::condition_variable state_cv;
    bool first_dispatch_returned = false;
    bool receipt_step_done = false;
    bool second_dispatch_started = false;
    std::chrono::steady_clock::time_point receipt_done_at{};
    std::chrono::steady_clock::time_point second_started_at{};
    dispatcher = [&](const chronon3d::runtime::job::NormalizedRenderJob&,
                     std::shared_ptr<chronon3d::SoftwareRenderer>,
                     std::shared_ptr<chronon3d::media::VideoJobExecutionContext>
                         video_execution) -> Reply {
        if (dispatch_count.fetch_add(1, std::memory_order_relaxed) == 0) {
            // The job layer's deferred receipt step (stands in for probe +
            // sha256 + decode gate): handed to the daemon's sink, to be run
            // off-lane after the execution lock + reservation release.
            std::function<void()> receipt_step = [&] {
                std::this_thread::sleep_for(700ms);
                {
                    std::lock_guard lock(state_mutex);
                    receipt_done_at = std::chrono::steady_clock::now();
                    receipt_step_done = true;
                }
                state_cv.notify_all();
            };
            video_execution->receipt_deferral(std::move(receipt_step));
            {
                std::lock_guard lock(state_mutex);
                first_dispatch_returned = true;
            }
            state_cv.notify_all();
            return Reply{Status::Ok, R"({"status":"ok"})"};
        }
        {
            std::lock_guard lock(state_mutex);
            second_started_at = std::chrono::steady_clock::now();
            second_dispatch_started = true;
        }
        state_cv.notify_all();
        return Reply{Status::Ok, R"({"status":"ok"})"};
    };

    std::thread daemon_thread([&service, &socket_path] {
        service.run_socket(socket_path.string());
    });

    UnixSocketClient client_a;
    UnixSocketClient client_b;
    bool connected = false;
    for (int attempt = 0; attempt < 200 && !connected; ++attempt) {
        if (std::filesystem::exists(socket_path)) {
            try {
                client_a.connect(socket_path.string());
                client_b.connect(socket_path.string());
                connected = true;
            } catch (const std::system_error&) {
                // The socket inode may appear just before listen() completes.
            }
        }
        if (!connected) std::this_thread::sleep_for(10ms);
    }
    REQUIRE(connected);

    const auto make_payload = [&](std::string_view job_id) {
        return std::string{R"({"job_id":")"} + std::string{job_id} +
        R"(","plan_path":")" + plan_path.string() +
        R"(","output":")" + output_path.string() +
        R"(","range_enabled":true,"first_frame":0,"last_frame":0,"output_spec":{"width":64,"height":64,"encoder_backend":"pipe","gpu_hot_path_mode":"auto"},"execution_requirements":{"gpu_required":false,"cpu_fallback_allowed":true,"composition_required":false,"video_source_required":false,"packet_copy_allowed":true}})";
    };

    std::optional<Reply> reply_a;
    std::chrono::steady_clock::time_point a_reply_received_at{};
    std::thread first_client([&] {
        reply_a = client_a.request(Command::RenderJob,
                                   make_payload("receipt-deferral-a"));
        a_reply_received_at = std::chrono::steady_clock::now();
    });

    bool first_returned = false;
    {
        std::unique_lock lock(state_mutex);
        first_returned = state_cv.wait_for(lock, 5s, [&] {
            return first_dispatch_returned;
        });
    }
    REQUIRE(first_returned);

    // The second job must be admitted and dispatched while the first job's
    // receipt step is (or was) still running off-lane.
    const auto reply_b = client_b.request(Command::RenderJob,
                                         make_payload("receipt-deferral-b"));
    first_client.join();

    REQUIRE(reply_a.has_value());
    CHECK(reply_a->status == Status::Ok);
    REQUIRE(reply_b.has_value());
    CHECK(reply_b->status == Status::Ok);
    {
        std::lock_guard lock(state_mutex);
        REQUIRE(receipt_step_done);
        REQUIRE(second_dispatch_started);
        // The render lane was already free when the second job started: the
        // receipt step had NOT completed yet. With the receipt inside the
        // admission lock this comparison reverses and the test fails.
        CHECK(second_started_at < receipt_done_at);
        // The reply of the deferred job still leaves only after the receipt
        // step completed (worker determinism).
        CHECK(receipt_done_at <= a_reply_received_at);
    }

    const auto shutdown = client_a.request(Command::Shutdown);
    REQUIRE(shutdown.has_value());
    CHECK(shutdown->status == Status::Shutdown);
    daemon_thread.join();
    client_a.close();
    client_b.close();
    dispatcher = std::move(previous_dispatcher);
}

TEST_CASE("daemon startup diagnostic reports resolved lane") {
    chronon3d::Config config = chronon3d::Config::from_environment();
    config.set_backend_preference(chronon3d::graph::BackendPreference::Software);
    chronon3d::RenderEngine engine(std::move(config));
    CHECK(engine.resolved_lane_name() == "software");

    const auto lane_msg = fmt::format("  resolved lane: {}", engine.resolved_lane_name());
    CHECK(lane_msg.find("resolved lane") != std::string::npos);
    CHECK(lane_msg.find("software") != std::string::npos);
}
