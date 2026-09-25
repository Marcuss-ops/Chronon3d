#include "daemon_service.hpp"
#include "daemon_service_internal.hpp"

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
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
    if (comp_id.empty()) {
        return ipc::Reply{ipc::Status::BadRequest, "PREPARE_PLAN requires a composition id"};
    }
    if (!m_registry.contains(comp_id)) {
        return ipc::Reply{ipc::Status::NotFound, "unknown composition '" + comp_id + "'"};
    }
    try {
        auto comp = m_registry.create(comp_id);
        std::shared_ptr<PreparedRenderJob> prepared;
        {
            std::lock_guard<std::mutex> engine_lock(m_engine_mutex);
            prepared = std::make_shared<PreparedRenderJob>(m_engine->prepare(comp));
        }
        std::size_t slots = 0;
        {
            std::lock_guard<std::mutex> lock(m_prepared_mutex);
            slots = prepared->resource_plan().slots.size();
            m_prepared_job = std::move(prepared);
            m_prepared_comp_id = comp_id;
        }
        spdlog::info("🧠 PREPARE_PLAN '{}' — resource plan slots: {}", comp_id, slots);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        m_prepared_job.reset();
        m_prepared_comp_id.clear();
        return ipc::Reply{ipc::Status::Error, std::string{"prepare failed: "} + e.what()};
    }
    return ipc::Reply{ipc::Status::Ok, "plan ready"};
}

ipc::Reply DaemonService::ipc_render_overlay(const std::string& args) {
    const auto tokens = daemon_detail::split_args(args);
    if (tokens.empty()) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "RENDER_OVERLAY requires '<frame> [output]'"};
    }
    std::shared_ptr<PreparedRenderJob> prepared;
    {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        prepared = m_prepared_job;
    }
    if (!prepared) {
        return ipc::Reply{ipc::Status::Error,
                          "no prepared plan — send PREPARE_PLAN first"};
    }

    Frame frame{0};
    try {
        frame = Frame(std::stoll(tokens[0]));
    } catch (const std::exception&) {
        return ipc::Reply{ipc::Status::BadRequest,
                          "invalid frame number: '" + tokens[0] + "'"};
    }

    std::string output = tokens.size() > 1 ? tokens[1] : "output/overlay_####.png";
    output = daemon_detail::format_output_path(output, static_cast<std::int32_t>(frame));
    std::lock_guard<std::mutex> overlay_lock(m_overlay_mutex);

    std::shared_ptr<Framebuffer> fb;
    try {
        fb = prepared->render(frame);
    } catch (const std::exception& e) {
        return ipc::Reply{ipc::Status::Error, std::string{"render failed: "} + e.what()};
    }
    if (!fb) {
        return ipc::Reply{ipc::Status::Error, "renderer returned a null framebuffer"};
    }

    std::filesystem::path out_path(output);
    if (out_path.has_parent_path()) std::filesystem::create_directories(out_path.parent_path());
    ImageWriteOptions write_opts;
    write_opts.format = image_format_from_path(output);
    if (!save_image(*fb, output, write_opts)) {
        return ipc::Reply{ipc::Status::Error, "failed to save frame to '" + output + "'"};
    }
    m_render_count.fetch_add(1, std::memory_order_relaxed);
    return ipc::Reply{ipc::Status::Ok, output};
}

ipc::Reply DaemonService::ipc_status() {
    std::string prepared_comp;
    {
        std::lock_guard<std::mutex> lock(m_prepared_mutex);
        prepared_comp = m_prepared_comp_id.empty() ? "(none)" : m_prepared_comp_id;
    }
    std::ostringstream ss;
    ss << "frames_rendered=" << m_render_count.load(std::memory_order_relaxed)
       << " total_ms=" << m_total_render_ms.load(std::memory_order_relaxed)
       << " prepared_comp=" << prepared_comp;
    return ipc::Reply{ipc::Status::Ok, ss.str()};
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
