#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/sdk/render_output.hpp
//
// P1-A — Skeleton SDK render output type.
// ═════════════════════════════════════════════════════════════════════════════
//
// `RenderOutput` is the value payload of
// `sdk::RenderEngine::render(...)`.  The pixel buffer is exposed as an
// opaque `const std::uint8_t*` pointer whose format is identified by
// the `PixelFormat` enum.  The buffer is owned by `RenderEngine::Impl`
// and remains valid until the next call to `render()` on the same
// engine instance or until the engine is destroyed.
//
// The OPP-side bridge will translate this to the canonical
// `chronon3d::core::memory::Framebuffer` for downstream consumers.
// ═════════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>

#include <chronon3d/sdk/render_request.hpp>   // sdk::Frame (peer sdk include)

namespace chronon3d::sdk {

/// Pixel-format identifier for `RenderOutput::pixels`.
enum class PixelFormat : std::uint8_t {
    Unknown = 0,
    Rgba8   = 1,   ///< 4 channels × 8 bits per channel
    Bgra8   = 2,   ///< 4 channels × 8 bits per channel (B, G, R, A order)
};

/// SDK-side render output.
struct RenderOutput {
    Frame           frame{0};                ///< Frame index that produced this output.
    std::int32_t    width{0};                ///< Output width in pixels (>0).
    std::int32_t    height{0};               ///< Output height in pixels (>0).
    PixelFormat     format{PixelFormat::Unknown}; ///< Pixel format of `pixels`.
    std::size_t     bytes_per_row{0};        ///< Row stride; 0 ⇒ tightly packed.
    const std::uint8_t* pixels{nullptr};     ///< Opaque pixel buffer owned by SDK impl.
    double          elapsed_milliseconds{0.0}; ///< Wall-clock render duration.
};

} // namespace chronon3d::sdk
