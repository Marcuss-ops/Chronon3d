#pragma once

// ---------------------------------------------------------------------------
// media/frame_source_provider.hpp
//
// Item 19 — Abstract interface for media frame decoding.
//
// The render graph should not depend on a concrete video backend; it only
// needs the ability to request a decoded framebuffer for a given source at
// a given frame.  This interface provides that abstraction.
//
// Concrete implementations live in backends/video/ and are wired into the
// software renderer by the runtime.
// ---------------------------------------------------------------------------

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/core/types/frame.hpp>

#include <memory>
#include <string>

namespace chronon3d::media {

class MediaFrameProvider {
public:
    virtual ~MediaFrameProvider() = default;

    /// Decode a single frame from the source identified by `path`.
    /// Output dimensions are suggested via `width`/`height`; implementations
    /// may scale or use the source's native size at their discretion.
    virtual std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame frame,
        int width,
        int height,
        float fps) = 0;
};

} // namespace chronon3d::media
