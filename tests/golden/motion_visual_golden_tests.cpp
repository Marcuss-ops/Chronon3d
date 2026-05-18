#include <doctest/doctest.h>

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <chronon3d/core/composition_registration.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace chronon3d;

#ifndef CHRONON3D_SOURCE_DIR
#define CHRONON3D_SOURCE_DIR "."
#endif

namespace {

SoftwareRenderer make_renderer() {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());
    return renderer;
}

std::shared_ptr<Framebuffer> load_png_as_framebuffer(const std::filesystem::path& path) {
    image::StbImageBackend backend;
    const auto buf = backend.load_image(path.string());
    if (!buf) {
        return nullptr;
    }

    auto fb = std::make_shared<Framebuffer>(buf->width, buf->height);
    for (int y = 0; y < buf->height; ++y) {
        for (int x = 0; x < buf->width; ++x) {
            const int idx = (y * buf->width + x) * 4;
            fb->set_pixel(x, y, Color{
                static_cast<float>(buf->pixels[idx + 0]) / 255.0f,
                static_cast<float>(buf->pixels[idx + 1]) / 255.0f,
                static_cast<float>(buf->pixels[idx + 2]) / 255.0f,
                static_cast<float>(buf->pixels[idx + 3]) / 255.0f,
            }.to_linear());
        }
    }
    return fb;
}

bool colors_near(const Color& a, const Color& b, float tolerance = 0.03f) {
    return std::abs(a.r - b.r) <= tolerance &&
           std::abs(a.g - b.g) <= tolerance &&
           std::abs(a.b - b.b) <= tolerance &&
           std::abs(a.a - b.a) <= tolerance;
}

void check_golden_frame(const std::string& comp_id, Frame frame, const std::filesystem::path& golden_path) {
    CompositionRegistry registry;
    auto comp = registry.create(comp_id);
    auto renderer = make_renderer();

    auto rendered = renderer.render_frame(comp, frame);
    REQUIRE(rendered != nullptr);

    std::filesystem::create_directories(golden_path.parent_path());
    if (!std::filesystem::exists(golden_path)) {
        REQUIRE(save_png(*rendered, golden_path.string()));
    }

    auto golden = load_png_as_framebuffer(golden_path);
    REQUIRE(golden != nullptr);
    REQUIRE(golden->width() == rendered->width());
    REQUIRE(golden->height() == rendered->height());

    std::size_t mismatched_pixels = 0;
    for (int y = 0; y < rendered->height(); ++y) {
        for (int x = 0; x < rendered->width(); ++x) {
            if (!colors_near(rendered->get_pixel(x, y), golden->get_pixel(x, y))) {
                ++mismatched_pixels;
            }
        }
    }

    CHECK_MESSAGE(
        mismatched_pixels == 0,
        "golden mismatch: comp=", comp_id,
        " frame=", frame,
        " mismatched_pixels=", mismatched_pixels
    );
}

std::filesystem::path golden_dir() {
    return std::filesystem::path(CHRONON3D_SOURCE_DIR) / "tests" / "golden" / "motion";
}

} // namespace

TEST_CASE("MotionObjectProof golden frame matches reference") {
    check_golden_frame(
        "MotionObjectProof",
        30,
        golden_dir() / "motion_object_proof_f0030.png"
    );
}

TEST_CASE("MotionPresetGallery golden frame matches reference") {
    check_golden_frame(
        "MotionPresetGallery",
        60,
        golden_dir() / "motion_preset_gallery_f0060.png"
    );
}

TEST_CASE("MotionDepthProof golden frame matches reference") {
    check_golden_frame(
        "MotionDepthProof",
        45,
        golden_dir() / "motion_depth_proof_f0045.png"
    );
}
