// SPDX-License-Identifier: MIT
//
// tests/io/test_image_writer_throw.cpp
//
// TICKET-RENDER-PIPELINE-INTEGRITY — layer 2 unit test.
//
// Lock the new contract: `save_png` throws `std::runtime_error` when
// the framebuffer contains a NaN or Inf pixel.  Previously the silent
// zero-fill guard produced an all-transparent PNG that looked like
// "the layer pipeline ran with no geometry".  After the fix the call
// fails loudly so the caller (write_frame_to_disk / save_image) can
// surface the error instead of committing a corrupt frame.
//
// The test exercises three sub-cases:
//   1. Single NaN pixel in a 4x4 framebuffer → throws (and the path is
//      reported with the offending (x,y)).
//   2. Single Inf pixel → also throws.
//   3. All-valid (solid color) framebuffer → does NOT throw, returns true.
//   4. The catch block in `save_image` (the layer 2 M1 hardening) converts
//      the throw into `return false`; this is the OBSERVABLE contract that
//      write_frame_to_disk relies on.

#include <doctest/doctest.h>
#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

using namespace chronon3d;

namespace {

// TICKET-RENDER-PIPELINE-INTEGRITY test helper: poison a single pixel
// with the requested channel values.  Pure set_pixel — no side effects.
void poison_pixel(Framebuffer& fb, int x, int y, float r, float g, float b, float a) {
    Color c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    fb.set_pixel(x, y, c);
}

} // namespace

TEST_CASE("image_writer: save_png throws std::runtime_error on a NaN pixel") {
    Framebuffer fb(4, 4);
    fb.clear(Color::red());
    // Poison pixel (1, 2) with a NaN in the R channel.
    poison_pixel(fb, 1, 2,
                 std::numeric_limits<float>::quiet_NaN(),
                 0.0f, 0.0f, 1.0f);

    // Pre-fix: this would silently zero-fill (1, 2) and return true.
    // Post-fix: must throw std::runtime_error with a message naming
    // the path AND the offending pixel coords.
    CHECK_THROWS_AS(
        save_png(fb, "test_throw_nan.png"),
        std::runtime_error);
}

TEST_CASE("image_writer: save_png throws std::runtime_error on an Inf pixel") {
    Framebuffer fb(4, 4);
    fb.clear(Color::red());
    poison_pixel(fb, 0, 0,
                 std::numeric_limits<float>::infinity(),
                 0.0f, 0.0f, 1.0f);

    CHECK_THROWS_AS(
        save_png(fb, "test_throw_inf.png"),
        std::runtime_error);
}

TEST_CASE("image_writer: save_png on a clean framebuffer does NOT throw and returns true") {
    Framebuffer fb(4, 4);
    fb.clear(Color::red());

    const std::string path = "test_image_writer_throw_clean.png";
    std::filesystem::remove(path); // ensure clean slate

    bool ok = false;
    REQUIRE_NOTHROW(ok = save_png(fb, path));
    CHECK(ok);
    CHECK(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST_CASE("image_writer: save_image catches save_png's throw and returns false") {
    // TICKET-RENDER-PIPELINE-INTEGRITY layer 2 M1 hardening: the
    // PNG branch in save_image is wrapped in try/catch.  The contract
    // observable to write_frame_to_disk is: a corrupt framebuffer
    // produces `save_image() == false` (NOT an uncaught exception
    // that crashes CLI main).  This test locks that contract.
    Framebuffer fb(4, 4);
    fb.clear(Color::red());
    poison_pixel(fb, 2, 2,
                 std::numeric_limits<float>::quiet_NaN(),
                 0.0f, 0.0f, 1.0f);

    const std::string path = "test_image_writer_save_image_catches.png";

    bool ok = true;
    REQUIRE_NOTHROW(ok = save_image(fb, path));
    CHECK_FALSE(ok);
    std::filesystem::remove(path);
}

TEST_CASE("image_writer: save_png throw message mentions path + coords") {
    // Locks the diagnostic value of the throw: the message must mention
    // the path AND the offending pixel (x, y) so a future SRE can grep
    // logs for the exact failure location without re-running.
    Framebuffer fb(4, 4);
    fb.clear(Color::red());
    poison_pixel(fb, 3, 1,
                 std::numeric_limits<float>::quiet_NaN(),
                 0.0f, 0.0f, 1.0f);

    const std::string path = "test_image_writer_throw_message.png";
    bool caught = false;
    std::string what;
    try {
        save_png(fb, path);
    } catch (const std::exception& e) {
        caught = true;
        what  = e.what();
    }
    REQUIRE(caught);
    CHECK(what.find(path)         != std::string::npos);
    CHECK(what.find("(3,1)")      != std::string::npos);
}
