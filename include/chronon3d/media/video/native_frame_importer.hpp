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

class NativeFrameImportSession {
public:
    virtual ~NativeFrameImportSession() = default;

    [[nodiscard]] virtual std::shared_ptr<Framebuffer> import(
        const NativeDecodedFrameView& frame) = 0;
};

class NativeFrameImporter {
public:
    virtual ~NativeFrameImporter() = default;

    [[nodiscard]] virtual std::unique_ptr<NativeFrameImportSession>
    create_session() = 0;
};

} // namespace chronon3d::media
