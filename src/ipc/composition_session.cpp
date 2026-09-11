// ---------------------------------------------------------------------------
// src/ipc/composition_session.cpp
// ---------------------------------------------------------------------------

#include "composition_session.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>
#include <nlohmann/json.hpp>

namespace chronon3d::ipc {

bool CompositionSession::contains(std::string_view id) const noexcept {
    std::lock_guard lock(m_mutex);
    // AUDIT-2026-09-11 — heterogeneous lookup: no throwaway std::string key.
    return m_compositions.contains(id);
}

void CompositionSession::remove(std::string_view id) {
    std::lock_guard lock(m_mutex);
    m_compositions.erase(id);
}

void CompositionSession::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_compositions.clear();
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

    std::lock_guard lock(m_mutex);
    m_compositions[std::string(composition_id)] = std::move(session);
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
    if (!session || !session->prepared) return nullptr;
    std::lock_guard render_lock(session->render_mutex);
    return session->prepared->render(frame);
}

} // namespace chronon3d::ipc
