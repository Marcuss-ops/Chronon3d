#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/runtime/render_surface.hpp>

#include <cstdint>
#include <memory>

namespace chronon3d::media {

/// Backend-neutral view of one hardware-decoded YUV frame.
/// Plane addresses are opaque to the media layer; the importer owns their
/// interpretation and synchronization with its backend.
struct NativeDecodedFrameView {
    const void* frame{nullptr};
    std::uint32_t width{0};
    std::uint32_t height{0};
    runtime::PixelFormat format{runtime::PixelFormat::Unknown};
    runtime::ColorMetadata color{};
};

/// Backend-neutral description of a decoded native YUV surface.  Device
/// pointers are opaque at this media boundary; the owning backend interprets
/// them and guarantees validity until the completion token is retired.
/// Pitches are explicit because CUDA/NVDEC surfaces are not required to be
/// tightly packed.
struct NativeYuvSurface {
    std::uint64_t y_device{0};
    std::uint64_t uv_device{0};
    std::uint32_t y_pitch{0};
    std::uint32_t uv_pitch{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    runtime::PixelFormat format{runtime::PixelFormat::Unknown};
    runtime::ColorMetadata color{};
    /// Opaque lifetime/completion identity, not a pointer owned by callers.
    std::uint64_t completion_token{0};
};

class NativeFrameImportSession {
public:
    virtual ~NativeFrameImportSession() = default;

    [[nodiscard]] virtual std::shared_ptr<Framebuffer> import(
        const NativeDecodedFrameView& frame) = 0;
};

class NativeFrameImporter {
public:
    virtual ~NativeFrameImporter() = default;

    [[nodiscard]] virtual void* cuda_context() const noexcept { return nullptr; }

    [[nodiscard]] virtual std::unique_ptr<NativeFrameImportSession>
    create_session() = 0;
};

} // namespace chronon3d::media
