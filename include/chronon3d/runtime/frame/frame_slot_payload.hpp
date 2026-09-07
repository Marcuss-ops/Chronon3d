#pragma once

// Temporary compatibility payload for the prepared-job pipeline.
//
// This is the single indexed payload authority for a frame slot while the
// evaluated-program representation is being lowered.  The composition-shaped
// value is intentionally kept behind this frame boundary; Wave 8 will replace
// it with the plain-data evaluated program state.

#include <chronon3d/core/memory/arena.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/timeline/evaluated_composition_frame.hpp>

#include <memory>
#include <optional>

namespace chronon3d::runtime {

struct FrameSlotPayload final {
    FrameArena arena{4u * 1024u * 1024u, true};
    std::optional<EvaluatedCompositionFrame> evaluated{};
    std::shared_ptr<Framebuffer> rendered{};

    void reset() noexcept {
        evaluated.reset();
        rendered.reset();
        arena.reset();
    }
};

}  // namespace chronon3d::runtime
