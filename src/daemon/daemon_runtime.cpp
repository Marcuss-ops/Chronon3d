#include "daemon_runtime.hpp"

#include <chronon3d/core/profiling/profiling.hpp>

#include <iomanip>
#include <sstream>

namespace chronon3d::daemon {
namespace {

const char* state_name(DaemonRuntime::State state) noexcept {
    switch (state) {
        case DaemonRuntime::State::Starting: return "starting";
        case DaemonRuntime::State::Warming: return "warming";
        case DaemonRuntime::State::Ready: return "ready";
        case DaemonRuntime::State::Draining: return "draining";
        case DaemonRuntime::State::Poisoned: return "poisoned";
    }
    return "unknown";
}

} // namespace

DaemonRuntime::DaemonRuntime(
    std::unique_ptr<RenderEngine> engine,
    const ipc::ContractValidatorRegistry&)
    : m_engine(std::move(engine)) {}

void DaemonRuntime::warmup() {
    m_state.store(State::Warming, std::memory_order_release);
    // RenderEngine construction owns the persistent backend/device lifetime.
    // The legacy PreparedRenderJob warmup was retired with the Composition
    // executor; the typed RENDER_JOB path prepares its program per job.
    m_state.store(State::Ready, std::memory_order_release);
}

std::string DaemonRuntime::health_json() const {
    const auto current = state();
    const bool healthy = current == State::Ready || current == State::Warming;
    std::ostringstream ss;
    ss << R"({"healthy":)" << (healthy ? "true" : "false")
       << R"(,"state":")" << state_name(current) << R"("})";
    return ss.str();
}

void DaemonRuntime::begin_shutdown() noexcept {
    State expected = State::Ready;
    (void)m_state.compare_exchange_strong(expected, State::Draining,
                                          std::memory_order_acq_rel);
}

bool DaemonRuntime::accepting() const noexcept {
    const auto current = state();
    return current == State::Ready || current == State::Warming;
}

DaemonRuntime::State DaemonRuntime::state() const noexcept {
    return m_state.load(std::memory_order_acquire);
}

std::string DaemonRuntime::status_json() const {
    std::ostringstream ss;
    ss << R"({"state":")" << state_name(state())
       << R"(","frames_rendered":)" << m_frame_count.load(std::memory_order_relaxed)
       << R"(,"total_render_ms":)" << std::fixed << std::setprecision(3)
       << m_total_render_ms.load(std::memory_order_relaxed)
       << R"(,"gpu_worker":"single-authority"})";
    return ss.str();
}

} // namespace chronon3d::daemon
