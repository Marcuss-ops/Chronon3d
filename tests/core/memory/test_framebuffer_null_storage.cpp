#include <doctest/doctest.h>

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/math/color.hpp>
#include <chronon3d/math/raster_utils.hpp>
#include <chronon3d/runtime/render_surface_handle.hpp>

#include <optional>
#include <vector>

using namespace chronon3d;

// =============================================================================
// TICKET-DAEMON-IPC-REJECTION-RESILIENCE — null-storage framebuffer clears.
//
// The native video lane allocates GPU-authoritative framebuffers that own no
// host storage:
//
//     auto* fb = new Framebuffer(w, h, static_cast<Color*>(nullptr));
//     fb->set_surface_handle(surface);
//     fb->mark_gpu_authoritative();
//
// (see src/render_graph/framebuffer_acquire_owned_detail.hpp and
// framebuffer_acquire_shared_detail.hpp).  The frame graph still drains clear
// paths over those buffers — including when a node fails mid-frame and the
// executor unwinds through a clear.  `clear()` / `clear(color, clip)` used to
// write straight through `data()`, so the null pointer segfaulted the daemon
// instead of letting the failed node surface a typed error envelope.
//
// Acceptance:
//   * clear() on a null-storage framebuffer is a no-op, never a null deref
//   * the opaque / content_cleared flags stay coherent across that no-op
//   * every branch of the clipped overload (full-frame, empty, partial) is
//     equally safe on null storage
//   * host-backed and external-storage clears still write pixels as before
// =============================================================================

namespace {

/// Mirrors the production construction for the native GPU encode lane: no host
/// storage, a live surface handle, GPU-authoritative.
Framebuffer make_gpu_only_framebuffer(i32 width, i32 height) {
    Framebuffer fb(width, height, static_cast<Color*>(nullptr));
    fb.set_surface_handle(static_cast<runtime::RenderSurfaceHandle>(0xC0FFEEu));
    fb.mark_gpu_authoritative();
    return fb;
}

} // namespace

TEST_CASE("Framebuffer: GPU-only buffers really own no host storage") {
    const auto fb = make_gpu_only_framebuffer(16, 16);

    REQUIRE(fb.data() == nullptr);
    CHECK(fb.width() == 16);
    CHECK(fb.height() == 16);
    CHECK(fb.is_gpu_authoritative());
    CHECK_FALSE(fb.is_cpu_authoritative());
    CHECK(fb.residency() == SurfaceResidency::GpuOnly);
}

TEST_CASE("Framebuffer: clear() on null host storage keeps flags coherent") {
    auto fb = make_gpu_only_framebuffer(16, 16);

    // Transparent: the buffer is fully clear but not opaque.
    fb.clear(Color::transparent());
    CHECK_FALSE(fb.is_opaque());
    CHECK(fb.is_content_cleared());

    // Opaque: the buffer is opaque and its content is no longer "cleared".
    fb.clear(Color::red());
    CHECK(fb.is_opaque());
    CHECK_FALSE(fb.is_content_cleared());

    // Translucent: must not claim opacity.
    fb.clear(Color{0.0f, 0.0f, 0.0f, 0.5f});
    CHECK_FALSE(fb.is_opaque());
    CHECK_FALSE(fb.is_content_cleared());
}

TEST_CASE("Framebuffer: clipped clear() on null host storage never dereferences") {
    auto fb = make_gpu_only_framebuffer(16, 16);

    // Full-frame clip delegates to the unclipped overload.
    fb.set_opaque(true);
    fb.set_content_cleared(false);
    fb.clear(Color::transparent(), raster::BBox{0, 0, 16, 16});
    CHECK_FALSE(fb.is_opaque());
    CHECK(fb.is_content_cleared());

    // A clip that resolves empty returns before touching storage.
    fb.set_opaque(true);
    fb.set_content_cleared(true);
    fb.clear(Color::red(), raster::BBox{20, 20, 24, 24});
    CHECK(fb.is_opaque());
    CHECK(fb.is_content_cleared());

    // A partial clip reaches the guarded strided write.  Clearing a region
    // with an opaque colour keeps the full-buffer opacity claim, but the
    // "whole buffer is transparent" claim must be dropped.
    fb.set_opaque(true);
    fb.set_content_cleared(true);
    fb.clear(Color::red(), raster::BBox{0, 0, 4, 4});
    CHECK(fb.is_opaque());
    CHECK_FALSE(fb.is_content_cleared());

    // No clip (nullopt) also routes through the unclipped overload.
    fb.clear(Color::red(), std::nullopt);
    CHECK(fb.is_opaque());
    CHECK_FALSE(fb.is_content_cleared());
}

TEST_CASE("Framebuffer: host-backed clears still write every pixel") {
    Framebuffer fb(4, 4);
    REQUIRE(fb.data() != nullptr);

    fb.clear(Color::red());
    CHECK(fb.is_opaque());
    CHECK_FALSE(fb.is_content_cleared());
    CHECK(fb.get_pixel(0, 0) == Color::red());
    CHECK(fb.get_pixel(3, 3) == Color::red());

    fb.clear(Color::transparent());
    CHECK_FALSE(fb.is_opaque());
    CHECK(fb.is_content_cleared());
    CHECK(fb.get_pixel(2, 2) == Color::transparent());
}

TEST_CASE("Framebuffer: external storage clears still write through the pointer") {
    constexpr i32 kWidth = 4;
    constexpr i32 kHeight = 4;
    std::vector<Color> storage(static_cast<usize>(kWidth) * kHeight,
                               Color::transparent());

    Framebuffer fb(kWidth, kHeight, storage.data());
    REQUIRE(fb.data() == storage.data());
    CHECK(fb.is_arena_allocated());

    fb.clear(Color::blue());
    CHECK(storage[0] == Color::blue());
    CHECK(storage[static_cast<usize>(kWidth) * kHeight - 1] == Color::blue());

    fb.clear(Color::green(), raster::BBox{1, 1, 2, 2});
    CHECK(storage[0] == Color::blue());
    CHECK(storage[static_cast<usize>(kWidth) + 1] == Color::green());
}
