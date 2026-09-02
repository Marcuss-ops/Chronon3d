// Regression lock for the final Blend2D -> Chronon framebuffer handoff.

#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/software/utils/blend2d_bridge.hpp>

#include <blend2d.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cstddef>

TEST_CASE("StaticTextRenderProducesVisiblePixels") {
    chronon3d::Framebuffer destination(64, 32);
    BLImage glyph_surface(16, 12, BL_FORMAT_PRGB32);
    {
        BLContext context(glyph_surface);
        context.setCompOp(BL_COMP_OP_SRC_COPY);
        context.setFillStyle(BLRgba32(255u, 255u, 255u, 255u));
        context.fillRect(BLRectI(2, 2, 12, 8));
        context.end();
    }

    chronon3d::blend2d_bridge::composite_bl_image_transformed(
        destination, glyph_surface,
        glm::translate(chronon3d::Mat4(1.0f), chronon3d::Vec3(24.0f, 10.0f, 0.0f)),
        1.0f, chronon3d::BlendMode::Normal);

    std::size_t visible = 0;
    for (int y = 0; y < destination.height(); ++y) {
        const auto* row = destination.pixels_row(y);
        for (int x = 0; x < destination.width(); ++x)
            visible += row[x].a > 0.01f && row[x].r > 0.01f;
    }

    CHECK(visible >= 50);
}
