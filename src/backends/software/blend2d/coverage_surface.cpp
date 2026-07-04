#include "coverage_surface.hpp"

#include <blend2d.h>
#include <utility>

namespace chronon3d::blend2d_bridge {

CoverageSurface::CoverageSurface(int width, int height)
    : m_width(width), m_height(height)
{
    if (width <= 0 || height <= 0) {
        m_width = 0;
        m_height = 0;
        return;
    }
    m_image.create(static_cast<int>(width),
                   static_cast<int>(height),
                   BL_FORMAT_A8);
    // Touch the data so the BL2 storage backend allocates and maps the
    // buffer; cache the row-0 pointer for `bytes()` fast paths.  If
    // `getData` fails (e.g. transient OOM in the storage backend) we
    // leave `m_cached_row == nullptr` and rely on per-pixel reads.
    BLImageData info{};
    if (m_image.getData(&info) == BL_SUCCESS && info.pixelData != nullptr) {
        m_cached_row = static_cast<uint8_t*>(info.pixelData);
    }
}

CoverageSurface::CoverageSurface(CoverageSurface&& other) noexcept
    : m_image(std::move(other.m_image)),
      m_width(other.m_width),
      m_height(other.m_height),
      m_cached_row(other.m_cached_row)
{
    other.m_width        = 0;
    other.m_height       = 0;
    other.m_cached_row   = nullptr;
}

CoverageSurface& CoverageSurface::operator=(CoverageSurface&& other) noexcept {
    if (this != &other) {
        m_image.reset();
        m_image        = std::move(other.m_image);
        m_width        = other.m_width;
        m_height       = other.m_height;
        m_cached_row   = other.m_cached_row;
        other.m_width        = 0;
        other.m_height       = 0;
        other.m_cached_row   = nullptr;
    }
    return *this;
}

void CoverageSurface::fill(uint8_t value) noexcept {
    if (m_image.empty()) return;
    // A8 fill via BLImage::fillRect — a single BL_COMP_OP_SRC_COPY pass
    // over a 1-channel constant.  Avoids the per-byte memset tax.
    BLContext ctx(m_image);
    ctx.setCompOp(BL_COMP_OP_SRC_COPY);
    ctx.setFillStyle(BLRgba32(value, value, value, value));
    ctx.fillAll();
    ctx.end();
}

float CoverageSurface::coverage(int x, int y) const noexcept {
    if (m_image.empty() || m_cached_row == nullptr) return 0.0f;
    if (x < 0 || x >= m_width || y < 0 || y >= m_height) return 0.0f;
    return static_cast<float>(m_cached_row[y * m_width + x]) * (1.0f / 255.0f);
}

} // namespace chronon3d::blend2d_bridge
