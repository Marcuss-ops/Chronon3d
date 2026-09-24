#include "daemon_service.hpp"
#include "daemon_render_concurrency.hpp"
#include "daemon_render_job_payload.hpp"
#include "daemon_service_internal.hpp"
#include "daemon_service_ipc_protocol.hpp"
#include "daemon_service_job_admission.hpp"

#include <chronon3d/core/profiling/profiling.hpp>
#include <chronon3d/runtime/job/render_job_request.hpp>

#include <spdlog/spdlog.h>

#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

namespace chronon3d::cli {

RenderJobDispatcher& render_job_dispatcher() {
    static RenderJobDispatcher dispatcher;
    return dispatcher;
}

void DaemonService::run_socket(const std::string& path) {
    ipc::UnixSocketServer server;
    try {
        server.listen(path);
    } catch (const std::system_error& e) {
        spdlog::error("Failed to bind Unix socket '{}': {}", path, e.what());
        return;
    }

    spdlog::info("🔌 Daemon listening on Unix socket '{}'", server.path());
    spdlog::info("   PREFETCH_ASSET | PREPARE_PLAN | RENDER_OVERLAY | RENDER_JOB | STATUS | SHUTDOWN");

    const int rc = server.serve_concurrent([this](const ipc::Request& req) {
        return handle_ipc(req);
    });

    if (rc != 0) {
        spdlog::error("Unix socket serve loop exited with error {} ({})",
                      rc, std::strerror(rc));
    }
    spdlog::info("Daemon socket shutdown. {} frames rendered in {:.1f}ms total.",
                 m_render_count.load(std::memory_order_relaxed),
                 m_total_render_ms.load(std::memory_order_relaxed));
}

ipc::Reply DaemonService::handle_ipc(const ipc::Request& req) {
    switch (req.cmd) {
        // Handlers own their domain lock; the dispatcher holds none. A
        // compile or a disk write therefore cannot serialize STATUS, and the
        // RENDER_JOB path below still runs entirely lock-free on the IPC
        // state (it carries its own scheduler reservation).
        case ipc::Command::PrefetchAsset:
            return ipc_prefetch_asset(req.payload);
        case ipc::Command::PreparePlan:
            return ipc_prepare_plan(req.payload);
        case ipc::Command::RenderOverlay:
            return ipc_render_overlay(req.payload);
        case ipc::Command::RenderJob:
            return handle_render_job_payload(req.payload);
        case ipc::Command::AssembleSegments:
            return ipc_assemble_segments(req.payload);
        case ipc::Command::Status:
            return ipc_status();
        case ipc::Command::Shutdown:
            m_running.store(false, std::memory_order_release);
            m_admission_cv.notify_all();
            return ipc::Reply{ipc::Status::Shutdown, "bye"};
        default:
            return ipc::Reply{ipc::Status::BadRequest, "unknown command"};
    }
}



} // namespace chronon3d::cli
