// ---------------------------------------------------------------------------
// src/ipc/composition_session.cpp
// ---------------------------------------------------------------------------

#include "composition_session.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace chronon3d::ipc {

bool CompositionSession::contains(std::string_view id) const noexcept {
    std::lock_guard lock(m_mutex);
    // AUDIT-2026-09-11 — heterogeneous lookup: no throwaway std::string key.
    const auto it = m_compositions.find(id);
    if (it == m_compositions.end() || !it->second) return false;
    std::lock_guard render_lock(it->second->render_mutex);
    return static_cast<bool>(it->second->prepared);
}

void CompositionSession::remove(std::string_view id) {
    std::lock_guard lock(m_mutex);
    // Heterogeneous erase. `std::map::erase` has no transparent overload (only
    // `const key_type&` and iterators), and `std::string`'s string_view
    // constructor is `explicit`, so `erase(id)` does not compile on a map with
    // `std::less<void>`: the throwaway `std::string` key would be required.
    // Find through the transparent comparator, then erase the iterator — the
    // same allocation-free lookup `contains()` above uses.
    if (const auto it = m_compositions.find(id); it != m_compositions.end()) {
        m_compositions.erase(it);
    }
    std::erase(m_terminal_job_history, std::string(id));
}

void CompositionSession::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_compositions.clear();
    m_terminal_job_history.clear();
}

void CompositionSession::create_composition(std::string_view composition_id,
                                            std::string_view descriptor_json) {
    using namespace chronon3d;

    std::lock_guard prepare_lock(m_prepare_mutex);

    auto* registry = m_engine->composition_registry();
    if (!registry) {
        throw std::runtime_error("CreateComposition: no registry set on engine");
    }

    auto doc = nlohmann::json::parse(descriptor_json);
    ContractValidationError validation_error;
    if (!m_validators->validate(
            ContractId::CompositionV1, doc, &validation_error)) {
        throw std::invalid_argument(
            "CreateComposition: " + validation_error.contract + ": " +
            validation_error.message);
    }

    CompositionDescriptor descriptor;
    descriptor.id       = doc.value("id", std::string(composition_id));
    descriptor.category = doc.value("category", "");
    descriptor.width    = doc.contains("width")
        ? std::optional<i32>(doc["width"].get<i32>()) : std::nullopt;
    descriptor.height   = doc.contains("height")
        ? std::optional<i32>(doc["height"].get<i32>()) : std::nullopt;
    descriptor.duration = doc.contains("duration")
        ? std::optional<Frame>(Frame(doc["duration"].get<std::int64_t>()))
        : std::nullopt;

    descriptor.prepare_props =
        [](const CompositionProps&) -> PreparedCompositionResult {
        PreparedComposition prepared;
        prepared.construct = []() -> Composition {
            CompositionSpec spec;
            spec.width  = 1920;
            spec.height = 1080;
            spec.frame_rate = FrameRate{30, 1};
            spec.duration = Frame{150};
            return Composition(spec,
                [](const FrameContext&) -> Scene { return Scene{}; });
        };
        return prepared;
    };

    const_cast<CompositionRegistry*>(registry)->add(std::move(descriptor));
    auto comp = const_cast<CompositionRegistry*>(registry)->create(composition_id);

    auto session = std::make_shared<SessionComposition>();
    session->composition_id = std::string(composition_id);
    // Prepare once at the daemon boundary. This performs resource preparation
    // and renderer warm-up before the composition becomes visible to clients;
    // RenderFrame then reuses the prepared slot/cache pipeline instead of
    // entering the generic compiled-composition path for every frame.
    session->prepared = std::make_unique<PreparedRenderJob>(m_engine->prepare(comp));
    session->engine = m_engine;
    session->job_state = SessionComposition::JobState::Prepared;
    session->status_message = "prepared";

    std::lock_guard lock(m_mutex);
    m_compositions[std::string(composition_id)] = std::move(session);
    std::erase(m_terminal_job_history, std::string(composition_id));
}

std::shared_ptr<Framebuffer> CompositionSession::render_frame(
    std::string_view composition_id, Frame frame) {
    std::shared_ptr<SessionComposition> session;
    {
        std::lock_guard lock(m_mutex);
        auto it = m_compositions.find(composition_id);
        if (it == m_compositions.end()) return nullptr;
        session = it->second;
    }
    if (!session) return nullptr;
    std::lock_guard render_lock(session->render_mutex);
    if (!session->prepared) return nullptr;
    {
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = SessionComposition::JobState::Running;
        session->last_frame = static_cast<std::uint32_t>(frame.integral());
        session->status_message = "rendering";
    }
    try {
        auto result = session->prepared->render(frame);
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = result
            ? SessionComposition::JobState::Completed
            : SessionComposition::JobState::Failed;
        session->status_message = result ? "completed" : "render returned null framebuffer";
        return result;
    } catch (const std::exception& e) {
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = SessionComposition::JobState::Failed;
        session->status_message = e.what();
        throw;
    } catch (...) {
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = SessionComposition::JobState::Failed;
        session->status_message = "non-standard render exception";
        throw;
    }
}

bool CompositionSession::release_job(std::string_view job_id) {
    std::shared_ptr<SessionComposition> session;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_compositions.find(job_id);
        if (it == m_compositions.end()) return false;
        session = it->second;
    }
    if (!session) return false;
    {
        std::lock_guard render_lock(session->render_mutex);
        session->prepared.reset();
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = SessionComposition::JobState::Released;
        session->status_message = "released";
    }
    remember_terminal_job(job_id);
    return true;
}

void CompositionSession::record_job_failure(
    std::string_view job_id, std::string_view message) {
    std::shared_ptr<SessionComposition> session;
    {
        std::lock_guard lock(m_mutex);
        const auto found = m_compositions.find(job_id);
        if (found != m_compositions.end()) return;
        session = std::make_shared<SessionComposition>();
        session->composition_id.assign(job_id);
        m_compositions.emplace(session->composition_id, session);
    }
    {
        std::lock_guard status_lock(session->status_mutex);
        session->job_state = SessionComposition::JobState::Failed;
        session->status_message.assign(message);
    }
    remember_terminal_job(job_id);
}

void CompositionSession::remember_terminal_job(std::string_view job_id) {
    std::lock_guard lock(m_mutex);
    const auto it = m_compositions.find(job_id);
    if (it == m_compositions.end() || !it->second) return;
    {
        std::lock_guard render_lock(it->second->render_mutex);
        if (it->second->prepared) return;
    }

    std::erase(m_terminal_job_history, std::string(job_id));
    m_terminal_job_history.emplace_back(job_id);
    while (m_terminal_job_history.size() > kRetainedTerminalJobStatuses) {
        const std::string oldest = std::move(m_terminal_job_history.front());
        m_terminal_job_history.pop_front();
        const auto expired = m_compositions.find(oldest);
        if (expired != m_compositions.end() && expired->second) {
            const auto expired_session = expired->second;
            std::lock_guard render_lock(expired_session->render_mutex);
            if (!expired_session->prepared) {
                std::lock_guard status_lock(expired_session->status_mutex);
                const auto state = expired_session->job_state;
                if (state == SessionComposition::JobState::Released ||
                    state == SessionComposition::JobState::Failed) {
                    m_compositions.erase(expired);
                }
            }
        }
    }
}

void CompositionSession::set_job_render_time(
    std::string_view job_id, double render_ms) {
    std::shared_ptr<SessionComposition> session;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_compositions.find(job_id);
        if (it == m_compositions.end()) return;
        session = it->second;
    }
    std::lock_guard status_lock(session->status_mutex);
    session->last_render_ms = render_ms;
}

std::optional<SessionJobStatus> CompositionSession::job_status(
    std::string_view job_id) const {
    std::shared_ptr<SessionComposition> session;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_compositions.find(job_id);
        if (it == m_compositions.end()) return std::nullopt;
        session = it->second;
    }
    if (!session) return std::nullopt;
    std::lock_guard status_lock(session->status_mutex);
    return SessionJobStatus{
        .job_id = session->composition_id,
        .state = session->job_state,
        .last_frame = session->last_frame,
        .message = session->status_message,
        .render_ms = session->last_render_ms,
    };
}

} // namespace chronon3d::ipc
