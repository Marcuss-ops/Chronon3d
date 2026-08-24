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
    return m_compositions.contains(std::string(id));
}

void CompositionSession::remove(std::string_view id) {
    std::lock_guard lock(m_mutex);
    m_compositions.erase(std::string(id));
}

void CompositionSession::clear() noexcept {
    std::lock_guard lock(m_mutex);
    m_compositions.clear();
}

void CompositionSession::create_composition(std::string_view composition_id,
                                            std::string_view descriptor_json) {
    using namespace chronon3d;

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

    auto compiled = chronon3d::compile_composition(comp, CompositionCompileContext{});
    if (!compiled) {
        throw std::runtime_error("compile failed: " + compiled.error().message);
    }

    SessionComposition session;
    session.composition_id = std::string(composition_id);
    session.compiled = std::move(compiled).value();
    session.engine = m_engine;

    std::lock_guard lock(m_mutex);
    m_compositions[std::string(composition_id)] = std::move(session);
}

std::shared_ptr<Framebuffer> CompositionSession::render_frame(
    std::string_view composition_id, Frame frame) {
    std::lock_guard lock(m_mutex);
    auto it = m_compositions.find(std::string(composition_id));
    if (it == m_compositions.end()) return nullptr;
    return it->second.engine->render_compiled(it->second.compiled, frame);
}

} // namespace chronon3d::ipc