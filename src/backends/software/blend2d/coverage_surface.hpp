#pragma once

// ──────────────────────────────────────────────────────────────────────
// coverage_surface.hpp — PR4 alpha-only coverage surface (A8)
//
// A8 (1 byte / pixel) is Blend2D's smallest alpha-only format and the
// canonical choice for separating geometric coverage from colour
// shading.  Memory footprint is 25% of PRGB32 for the same ROI, and
// readback into a Chronon `Color::a` channel via `coverage(x, y)`
// is a single `uint8_t` load divided by 255.
//
// Lifetime:   Move-construction is supported so callers can stream
//             the BLImage out of the BL2 rasterizer's BLContext and
//             hand it to the Chronon shade/composite loop without
//             triggering BL2's internal refcount churn.
//
// Read access: `coverage(x, y)` reads alpha in [0..1]; `bytes()`
//              exposes a flat pointer for vectorised composers.
// Write access: `fill(BLRgba32)` zero-clears or 0xFF-fills the
//               surface — useful for resetting between draw passes
//               without reallocating the BLImage.
// ──────────────────────────────────────────────────────────────────────

#include <blend2d.h>
#include <cstddef>
#include <cstdint>

namespace chronon3d::blend2d_bridge {

class CoverageSurface {
public:
    CoverageSurface() = default;
    explicit CoverageSurface(int width, int height);

    CoverageSurface(const CoverageSurface&) = delete;
    CoverageSurface& operator=(const CoverageSurface&) = delete;
    CoverageSurface(CoverageSurface&& other) noexcept;
    CoverageSurface& operator=(CoverageSurface&& other) noexcept;

    ~CoverageSurface() = default;

    [[nodiscard]] BLImage&       image()       noexcept { return m_image; }
    [[nodiscard]] const BLImage& image() const noexcept { return m_image; }

    [[nodiscard]] int width()  const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] bool empty() const noexcept { return m_width <= 0 || m_height <= 0; }

    /// Reset every pixel's coverage to `value` (0x00 erase, 0xFF opaque).
    /// Calls `BLImage::fill` with an A8 constant for bulk zero cost.
    void fill(uint8_t value) noexcept;

    /// Read coverage at (x, y) in surface-local coordinates.
    /// Out-of-bounds clamps to 0.0f so the per-pixel loop never needs
    /// to bounds-check when iterating straight over the ROI.
    [[nodiscard]] float coverage(int x, int y) const noexcept;

    /// Raw pointer to the A8 buffer.  Returns the row-0 pointer
    /// cached at construction; assumes BL2 allocates A8 with
    /// `stride == width` (true for every storage backend we have
    /// measured in CI, but stale if a caller mutates `image()` after
    /// construction invalidates the underlying buffer).  Prefer
    /// `coverage(x, y)` for arbitrary reads.
    [[nodiscard]] const uint8_t* bytes() noexcept { return m_cached_row; }

private:
    BLImage m_image;
    int     m_width{0};
    int     m_height{0};
    // Cache the row-0 pointer once BL2 has allocated the data buffer.
    // `bytes()` returns this; if the image has not been initialised
    // (`m_width == 0`), we fall back to nullptr so callers don't deref
    // a live-but-empty BLImage handle.
    uint8_t* m_cached_row{nullptr};

    friend class CoverageSurfaceAccess;
};

} // namespace chronon3d::blend2d_bridge
