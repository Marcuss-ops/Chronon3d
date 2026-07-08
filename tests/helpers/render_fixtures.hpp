#pragma once

#include <tests/helpers/test_utils.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <memory>
#include <string>
#include <filesystem>

namespace chronon3d::test {

inline std::shared_ptr<Framebuffer> render_modular(const Composition& comp, Frame frame = 0) {
    auto renderer = make_renderer_shared();
    RenderSettings settings = renderer->render_settings();
    settings.diagnostics.enabled = true;
    renderer->set_settings(settings);
    return renderer->render(comp, frame);
}

inline void save_debug(const Framebuffer& fb, const std::string& path) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    save_png(fb, path);
}

} // namespace chronon3d::test
