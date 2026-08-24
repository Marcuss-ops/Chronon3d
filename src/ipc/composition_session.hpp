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

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace chronon3d::ipc {

/// Result of a CreateComposition IPC request.
struct SessionComposition {
    std::string composition_id;
    CompiledComposition compiled;
    RenderEngine* engine{nullptr};
};

/// Per-daemon-session composition cache.  Uses the canonical
/// CompositionRegistry when available for descriptor resolution.
class CompositionSession {
public:
    explicit CompositionSession(RenderEngine& engine)
        : m_engine(&engine) {}

    void create_composition(std::string_view composition_id,
                            std::string_view descriptor_json);

    [[nodiscard]] std::shared_ptr<Framebuffer> render_frame(
        std::string_view composition_id, Frame frame);

    [[nodiscard]] bool contains(std::string_view composition_id) const noexcept;
    void remove(std::string_view composition_id);
    void clear() noexcept;

private:
    RenderEngine* m_engine;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, SessionComposition> m_compositions;
};

} // namespace chronon3d::ipc