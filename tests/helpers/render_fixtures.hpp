#pragma once

#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include <memory>
#include <string>
#include <filesystem>

namespace chronon3d::test {

inline std::unique_ptr<Framebuffer> render_modular(const Composition& comp, Frame frame = 0) {
    SoftwareRenderer renderer;
    RenderSettings settings;
    settings.use_modular_graph = true;
    renderer.set_settings(settings);
    renderer.set_font_backend(std::make_shared<text::StbFontBackend>());
    return renderer.render_frame(comp, frame);
}

inline void save_debug(const Framebuffer& fb, const std::string& path) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    save_png(fb, path);
}

} // namespace chronon3d::test
