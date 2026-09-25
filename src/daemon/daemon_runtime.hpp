#pragma once

#include "../ipc/composition_session.hpp"
#include "../ipc/contract_validator_registry.hpp"

#include <chronon3d/api/render_engine.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::daemon {

/// Single rendering authority owned by the persistent daemon process.
///
/// IPC handlers may be concurrent. CompositionSession owns map/lifecycle
/// synchronization and serializes only frames targeting the same prepared
/// composition, so unrelated daemon clients do not queue behind one global
/// render mutex.
class DaemonRuntime {
public:
    struct RenderOutcome {
        std::shared_ptr<Framebuffer> framebuffer;
        double render_ms{0.0};
    };
    enum class State : std::uint8_t {
        Starting,
        Warming,
        Ready,
        Draining,
        Poisoned,
    };

    DaemonRuntime(std::unique_ptr<RenderEngine> engine,
                  const ipc::ContractValidatorRegistry& validators);

    DaemonRuntime(const DaemonRuntime&) = delete;
    DaemonRuntime& operator=(const DaemonRuntime&) = delete;

    /// The current RenderEngine remains alive for the entire daemon lifetime.
    void warmup();

    void prepare_composition(std::string_view composition_id,
                             std::string_view descriptor_json);
    void prepare_job(std::string_view job_id, std::string_view descriptor_json);
    [[nodiscard]] std::shared_ptr<Framebuffer> render_frame(
        std::string_view composition_id, Frame frame);
    [[nodiscard]] RenderOutcome render_frame_timed(
        std::string_view composition_id, Frame frame);
    [[nodiscard]] RenderOutcome render_job(
        std::string_view job_id, Frame frame);
    [[nodiscard]] RenderEngine& engine() noexcept { return *m_engine; }
    [[nodiscard]] bool contains(std::string_view composition_id) const noexcept;
    void release_composition(std::string_view composition_id);
    [[nodiscard]] bool release_job(std::string_view job_id);
    void record_job_failure(std::string_view job_id, std::string_view message);
    [[nodiscard]] std::optional<ipc::SessionJobStatus> job_status(
        std::string_view job_id) const;
    [[nodiscard]] std::string health_json() const;

    void begin_shutdown() noexcept;
    [[nodiscard]] bool accepting() const noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] std::string status_json() const;

private:
    std::unique_ptr<RenderEngine> m_engine;
    ipc::CompositionSession m_session;
    std::atomic<State> m_state{State::Starting};
    std::atomic<std::uint64_t> m_frame_count{0};
    std::atomic<double> m_total_render_ms{0.0};
};

} // namespace chronon3d::daemon
