#include "daemon_service.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace chronon3d::cli {

ipc::Reply DaemonService::ipc_prefetch_asset(const std::string& path) {
    if (path.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREFETCH_ASSET requires an asset path"};
    }
    std::lock_guard<std::mutex> engine_lock(m_engine_mutex);
    const auto id = m_engine->assets().import_by_extension(path);
    if (!id) {
        return ipc::Reply{ipc::Status::NotFound,
                          "unrecognized asset extension: '" + path + "'"};
    }
    spdlog::info("📦 PREFETCH_ASSET {} (id {})", path, *id);
    return ipc::Reply{ipc::Status::Ok, "prefetched"};
}

ipc::Reply DaemonService::ipc_prepare_plan(const std::string& comp_id) {
    (void)comp_id;
    return ipc::Reply{ipc::Status::Error,
        "PREPARE_PLAN was retired with the legacy Composition executor; "
        "submit a chronon.render-plan.v2 job through RENDER_JOB"};
}

ipc::Reply DaemonService::ipc_render_overlay(const std::string& args) {
    (void)args;
    return ipc::Reply{ipc::Status::Error,
        "RENDER_OVERLAY was retired with the legacy Composition executor; "
        "submit a chronon.render-plan.v2 job through RENDER_JOB"};
}

ipc::Reply DaemonService::ipc_status() {
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count.load(std::memory_order_relaxed)
       << " total_ms=" << m_total_render_ms.load(std::memory_order_relaxed);
    return ipc::Reply{ipc::Status::Ok, ss.str()};
}

ipc::Reply DaemonService::ipc_cache_stats() {
    return ipc::Reply{ipc::Status::Ok, format_cache_stats()};
}

bool DaemonService::begin_job_status(std::string_view job_id) {
    if (job_id.empty()) return true;
    std::lock_guard lock(m_job_status_mutex);
    return m_job_statuses.emplace(
        std::string{job_id}, JobStatusRecord{JobState::Prepared, "prepared"}).second;
}

void DaemonService::update_job_status(
    std::string_view job_id, JobState state, std::string message) {
    if (job_id.empty()) return;
    std::lock_guard lock(m_job_status_mutex);
    const auto it = m_job_statuses.find(job_id);
    if (it == m_job_statuses.end()) return;
    it->second = JobStatusRecord{state, std::move(message)};
    if (state != JobState::Completed && state != JobState::Failed) return;

    std::erase(m_terminal_job_history, std::string{job_id});
    m_terminal_job_history.emplace_back(job_id);
    while (m_terminal_job_history.size() > kRetainedTerminalJobStatuses) {
        const auto oldest = std::move(m_terminal_job_history.front());
        m_terminal_job_history.pop_front();
        const auto expired = m_job_statuses.find(oldest);
        if (expired != m_job_statuses.end() &&
            (expired->second.state == JobState::Completed ||
             expired->second.state == JobState::Failed)) {
            m_job_statuses.erase(expired);
        }
    }
}

ipc::Reply DaemonService::ipc_get_job_status(const std::string& job_id) {
    if (job_id.empty()) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "GET_JOB_STATUS requires a job_id"};
    }
    std::lock_guard lock(m_job_status_mutex);
    const auto it = m_job_statuses.find(job_id);
    if (it == m_job_statuses.end()) {
        return ipc::Reply{ipc::Status::NotFound,
                          "unknown or expired job_id '" + job_id + "'"};
    }
    const char* state = "unknown";
    switch (it->second.state) {
    case JobState::Prepared: state = "prepared"; break;
    case JobState::Running: state = "running"; break;
    case JobState::Completed: state = "completed"; break;
    case JobState::Failed: state = "failed"; break;
    }
    return ipc::Reply{ipc::Status::Ok,
        nlohmann::json{{"job_id", job_id}, {"state", state},
                       {"message", it->second.message}}.dump()};
}

} // namespace chronon3d::cli
