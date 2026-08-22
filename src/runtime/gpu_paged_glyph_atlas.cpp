#include <chronon3d/runtime/gpu_paged_glyph_atlas.hpp>

#include <cmath>
#include <cstring>

namespace chronon3d::runtime {

void GpuPagedGlyphAtlas::attach(RenderSurfaceRegistry& registry,
                                graph::RenderBackend& backend) noexcept {
    m_registry = &registry;
    m_backend  = &backend;
}

std::pair<std::size_t, GpuPagedGlyphAtlas::PageSlot>
GpuPagedGlyphAtlas::allocate_slot(std::uint32_t width, std::uint32_t height) {
    const auto w16 = static_cast<std::uint16_t>(width);
    const auto h16 = static_cast<std::uint16_t>(height);
    if (w16 == 0 || h16 == 0 || w16 > kPageSize || h16 > kPageSize) {
        return {0, {0, 0, 0, 0}};  // caller must guard
    }

    // Try to fit in an existing page
    for (std::size_t page_idx = 0; page_idx < m_pages.size(); ++page_idx) {
        auto& page = m_pages[page_idx];
        if (page.cursor_x + w16 > kPageSize) {
            // Next row
            page.cursor_x = 0;
            page.cursor_y += page.row_height;
            page.row_height = 0;
        }
        if (page.cursor_y + h16 <= kPageSize) {
            const PageSlot slot{page.cursor_x, page.cursor_y, w16, h16};
            page.cursor_x += w16;
            page.row_height = std::max(page.row_height, h16);
            return {page_idx, slot};
        }
    }

    // Allocate a new page
    const auto page_count = m_pages.size();
    AtlasPage new_page;
    new_page.handle = kInvalidRenderSurfaceHandle;  // created lazily on first upload
    new_page.cursor_x = w16;
    new_page.cursor_y = 0;
    new_page.row_height = h16;
    m_pages.push_back(std::move(new_page));
    return {page_count, {0, 0, w16, h16}};
}

PagedGlyphLocation GpuPagedGlyphAtlas::acquire(
    const GpuGlyphKey& key,
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> coverage) {
    if (width == 0 || height == 0 || coverage.empty()) {
        return PagedGlyphLocation{};
    }

    std::lock_guard lock(m_mutex);

    // Cache hit
    {
        const auto it = m_locations.find(key);
        if (it != m_locations.end()) {
            ++m_stats.cache_hits;
            return it->second;
        }
    }

    ++m_stats.cache_misses;
    ++m_stats.total_glyphs;

    // Allocate slot
    const auto [page_idx, slot] = allocate_slot(width, height);
    if (slot.w == 0) return PagedGlyphLocation{};  // safety

    auto& page = m_pages[page_idx];

    // Create the page surface if not yet created
    if (page.handle == kInvalidRenderSurfaceHandle && m_registry && m_backend) {
        // Fase C: allocate Rgba32Float pages (R8Unorm deferred to shader + backend work).
        // The atlas stores coverage in the alpha channel (R=G=B=0, A=coverage);
        // the instance colour is applied by the shader.
        const SurfaceDesc desc{kPageSize, kPageSize, PixelFormat::Rgba32Float,
                               ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
        page.handle = m_registry->create(desc);
        if (page.handle != kInvalidRenderSurfaceHandle) {
            const auto created = m_backend->create_surface(page.handle, desc);
            if (!created.ok()) {
                m_registry->release(page.handle);
                page.handle = kInvalidRenderSurfaceHandle;
            } else {
                // Zero-fill the page on creation so unoccupied regions are
                // transparent. We use fill_rect_surface to clear the whole page.
                (void)m_backend->fill_rect_surface(page.handle, 0, 0,
                    static_cast<std::int32_t>(kPageSize),
                    static_cast<std::int32_t>(kPageSize), Color{});
                ++m_stats.pages_allocated;
            }
        }
    }

    // Upload the glyph coverage into the page
    if (page.handle != kInvalidRenderSurfaceHandle && m_backend) {
        // Convert R8 coverage to RGBA32F: zero RGB, alpha=coverage.
        // The shader reads atlas alpha and applies instance colour.
        const std::size_t num_pixels = static_cast<std::size_t>(width) * height;
        std::vector<float> rgba(num_pixels * 4, 0.0f);
        for (std::size_t i = 0; i < num_pixels; ++i) {
            const float a = static_cast<float>(coverage[i]) / 255.0f;
            rgba[i * 4 + 3] = a;  // alpha = coverage, RGB = 0
        }
        const SurfaceDesc sub_desc{width, height, PixelFormat::Rgba32Float,
                                   ResourceUsage::Storage, LifetimeClass::JobPersistent, 0};
        const auto uploaded = m_backend->upload_surface_region(
            page.handle, sub_desc,
            static_cast<std::int32_t>(slot.x), static_cast<std::int32_t>(slot.y),
            width, height, rgba);
        if (uploaded.ok()) {
            page.uploaded = true;
        }
    }

    // Compute UV coords
    const float inv_size = 1.0f / static_cast<float>(kPageSize);
    PagedGlyphLocation loc;
    loc.page = static_cast<std::uint16_t>(page_idx);
    loc.x    = slot.x;
    loc.y    = slot.y;
    loc.w    = slot.w;
    loc.h    = slot.h;
    loc.u0   = static_cast<float>(slot.x) * inv_size;
    loc.v0   = static_cast<float>(slot.y) * inv_size;
    loc.u1   = static_cast<float>(slot.x + slot.w) * inv_size;
    loc.v1   = static_cast<float>(slot.y + slot.h) * inv_size;

    m_locations.emplace(key, loc);
    m_stats.resident_glyphs = m_locations.size();
    return loc;
}

std::optional<PagedGlyphLocation> GpuPagedGlyphAtlas::lookup(
    const GpuGlyphKey& key) const noexcept {
    std::lock_guard lock(m_mutex);
    const auto it = m_locations.find(key);
    if (it == m_locations.end()) return std::nullopt;
    return it->second;
}

RenderSurfaceHandle GpuPagedGlyphAtlas::page_handle(
    std::size_t page_index) const noexcept {
    std::lock_guard lock(m_mutex);
    if (page_index >= m_pages.size()) return kInvalidRenderSurfaceHandle;
    return m_pages[page_index].handle;
}

void GpuPagedGlyphAtlas::clear() noexcept {
    std::lock_guard lock(m_mutex);
    if (m_backend && m_registry) {
        for (auto& page : m_pages) {
            if (page.handle != kInvalidRenderSurfaceHandle) {
                (void)m_backend->release_surface(page.handle);
                m_registry->release(page.handle);
            }
        }
    }
    m_pages.clear();
    m_locations.clear();
    m_stats = {};
}

PagedGlyphAtlasStats GpuPagedGlyphAtlas::stats() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

} // namespace chronon3d::runtime