#pragma once

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <memory>

namespace chronon3d::test {

inline std::unique_ptr<SoftwareRenderer> create_test_renderer() {
    auto r = std::make_unique<SoftwareRenderer>();
    r->set_image_backend(std::make_shared<image::StbImageBackend>());
    r->set_font_backend(std::make_shared<text::StbFontBackend>());
    return r;
}

} // namespace chronon3d::test
