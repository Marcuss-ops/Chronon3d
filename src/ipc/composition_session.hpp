// ---------------------------------------------------------------------------
// src/ipc/composition_session.hpp
//
// ADR-024: A daemon session caches compiled compositions keyed by
// composition_id.  This is a PER-SESSION cache (not a global registry) —
// canonical registries (CompositionRegistry) are NOT duplicated here.
// The session reuses the existing CompositionRegistry for descriptor
// resolution and stores the compiled result so RenderFrame calls reuse the
// same CompiledFrameGraph across frames.
// ---------------------------------------------------------------------------

#pragma once

#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/timeline/compile_evaluate.hpp>
#include <chronon3d/scene/model/core/scene.hpp>

#include "contract_validator_registry.hpp"

#include <memory>
#include <cstdint>
#include <optional>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <map>

namespace chronon3d::ipc {

/// Result of a CreateComposition IPC request.
struct SessionComposition {
    std::string composition_id;
    std::unique_ptr<PreparedRenderJob> prepared;
    RenderEngine* engine{nullptr};
    // Composition-local lifecycle authority. Independent compositions may be
    // rendered concurrently; only frames targeting the same prepared job are
    // serialized because PreparedRenderJob owns mutable temporal state.
    mutable std::mutex render_mutex;
    mutable std::mutex status_mutex;
    enum class JobState : std::uint8_t {
        Unknown,
        Prepared,
        Running,
        Completed,
        Failed,
        Released,
    };
    JobState job_state{JobState::Unknown};
    std::uint32_t last_frame{0};
    std::string status_message;
    double last_render_ms{0.0};
};

struct SessionJobStatus {
    std::string job_id;
    SessionComposition::JobState state{SessionComposition::JobState::Unknown};
    std::uint32_t last_frame{0};
    std::string message;
    double render_ms{0.0};
};

/// Per-daemon-session composition cache.  Uses the canonical
/// CompositionRegistry when available for descriptor resolution.
class CompositionSession {
public:
    explicit CompositionSession(RenderEngine& engine,
                                const ContractValidatorRegistry& validators)
        : m_engine(&engine), m_validators(&validators) {}

    void create_composition(std::string_view composition_id,
                            std::string_view descriptor_json);

    [[nodiscard]] std::shared_ptr<Framebuffer> render_frame(
        std::string_view composition_id, Frame frame);

    [[nodiscard]] bool release_job(std::string_view job_id);
    void record_job_failure(std::string_view job_id, std::string_view message);
    void set_job_render_time(std::string_view job_id, double render_ms);
    [[nodiscard]] std::optional<SessionJobStatus> job_status(
        std::string_view job_id) const;

    [[nodiscard]] bool contains(std::string_view composition_id) const noexcept;
    void remove(std::string_view composition_id);
    void clear() noexcept;

private:
    void remember_terminal_job(std::string_view job_id);

    RenderEngine* m_engine;
    const ContractValidatorRegistry* m_validators;
    // Engine/registry mutation is a lifecycle operation, separate from the
    // per-composition render locks below.
    mutable std::mutex m_prepare_mutex;
    mutable std::mutex m_mutex;
    // AUDIT-2026-09-11 — std::map with transparent comparator enables
    // heterogeneous find/contains/erase on string_view keys (unordered_map
    // heterogeneous lookup is C++26); the session holds a handful of
    // compositions so the ordered container's O(log n) is irrelevant, and
    // every lookup stop allocating a throwaway std::string key.
    std::map<std::string, std::shared_ptr<SessionComposition>, std::less<>> m_compositions;
    std::deque<std::string> m_terminal_job_history;
    static constexpr std::size_t kRetainedTerminalJobStatuses = 256;
};

} // namespace chronon3d::ipc
