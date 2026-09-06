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
    const ipc::ContractValidatorRegistry& validators)
    : m_engine(std::move(engine))
    , m_session(*m_engine, validators) {}

void DaemonRuntime::warmup() {
    m_state.store(State::Warming, std::memory_order_release);
    // RenderEngine construction owns the persistent backend/device lifetime.
    // Resource-specific warmup is performed by PreparedRenderJob::prepare;
    // keeping this explicit state boundary prevents advertising READY before
    // the daemon has completed its startup phase.
    m_state.store(State::Ready, std::memory_order_release);
}

void DaemonRuntime::prepare_composition(
    std::string_view composition_id, std::string_view descriptor_json) {
    if (!accepting()) throw std::runtime_error("daemon is not accepting jobs");
    std::lock_guard lock(m_render_mutex);
    m_session.create_composition(composition_id, descriptor_json);
}

std::shared_ptr<Framebuffer> DaemonRuntime::render_frame(
    std::string_view composition_id, Frame frame) {
    return render_frame_timed(composition_id, frame).framebuffer;
}

DaemonRuntime::RenderOutcome DaemonRuntime::render_frame_timed(
    std::string_view composition_id, Frame frame) {
    RenderOutcome outcome;
    if (!accepting()) return {};
    std::lock_guard lock(m_render_mutex);
    const auto t0 = profiling::now();
    outcome.framebuffer = m_session.render_frame(composition_id, frame);
    outcome.render_ms = profiling::elapsed_ms(t0);
    if (outcome.framebuffer) {
        m_frame_count.fetch_add(1, std::memory_order_relaxed);
        double old = m_total_render_ms.load(std::memory_order_relaxed);
        while (!m_total_render_ms.compare_exchange_weak(
            old, old + outcome.render_ms, std::memory_order_relaxed)) {}
    }
    return outcome;
}

bool DaemonRuntime::contains(std::string_view composition_id) const noexcept {
    return m_session.contains(composition_id);
}

void DaemonRuntime::release_composition(std::string_view composition_id) {
    std::lock_guard lock(m_render_mutex);
    m_session.remove(composition_id);
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
