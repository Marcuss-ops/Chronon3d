#pragma once

#include "../ipc/contract_validator_registry.hpp"

#include <chronon3d/api/render_engine.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace chronon3d::daemon {

/// Single rendering authority owned by the persistent daemon process.
///
/// The legacy Composition session (create-composition / prepare-job /
/// render-frame) was retired together with the CompiledComposition executor.
/// The daemon keeps the engine alive and reports lifecycle state; execution
/// enters through the typed RenderPlan/RENDER_JOB path.
class DaemonRuntime {
public:
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

    [[nodiscard]] RenderEngine& engine() noexcept { return *m_engine; }

    [[nodiscard]] std::string health_json() const;

    void begin_shutdown() noexcept;
    [[nodiscard]] bool accepting() const noexcept;
    [[nodiscard]] State state() const noexcept;
    [[nodiscard]] std::string status_json() const;

private:
    std::unique_ptr<RenderEngine> m_engine;
    std::atomic<State> m_state{State::Starting};
    std::atomic<std::uint64_t> m_frame_count{0};
    std::atomic<double> m_total_render_ms{0.0};
};

} // namespace chronon3d::daemon
