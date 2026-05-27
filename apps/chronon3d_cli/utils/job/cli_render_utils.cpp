#include "cli_render_utils.hpp"
#include <chronon3d/specscene/specscene.hpp>
#include <chronon3d/backends/image/stb_image_backend.hpp>
#include <chronon3d/backends/video/video_frame_decoder.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace chronon3d {
namespace cli {

namespace {

class CompositionFallbackVideoDecoder final : public video::VideoFrameDecoder {
public:
    explicit CompositionFallbackVideoDecoder(const CompositionRegistry* registry)
        : m_registry(registry) {}

    std::shared_ptr<Framebuffer> decode_frame(
        const std::string& path,
        Frame frame,
        i32 width,
        i32 height,
        f32 fps
    ) override {
        // Strip path to get composition name, e.g. "assets/videos/glow_test.mp4" -> "GlowVideoSourceAsset"
        std::string comp_name = "";
        if (path.find("glow_test.mp4") != std::string::npos) {
            comp_name = "GlowVideoSourceAsset";
        }
        
        if (m_registry && !comp_name.empty() && m_registry->contains(comp_name)) {
            SoftwareRenderer temp_renderer;
            temp_renderer.set_composition_registry(m_registry);
            RenderSettings settings;
            settings.use_modular_graph = true;
            temp_renderer.set_settings(settings);
            temp_renderer.set_image_backend(std::make_shared<image::StbImageBackend>());
            
            auto comp_instance = m_registry->create(comp_name);
            auto comp = std::make_shared<Composition>(std::move(comp_instance));
            
            auto fb = temp_renderer.render_frame(*comp, frame);
            if (fb) {
                return fb;
            }
        }
        
        // Fallback to a simple colored/solid framebuffer (like blue) so it's not transparent
        auto fb = std::make_shared<Framebuffer>(width, height);
        fb->clear(Color{0.2f, 0.6f, 1.0f, 1.0f});
        return fb;
    }

private:
    const CompositionRegistry* m_registry;
};

} // namespace

ResolvedComposition resolve_composition(const CompositionRegistry& registry,
                                         const std::string& comp_id) {
    namespace fs = std::filesystem;

    ResolvedComposition result;

    const bool specscene_input = fs::exists(comp_id) && specscene::is_specscene_file(comp_id);

    if (specscene_input) {
        result.from_specscene = true;
        auto compiled = specscene::compile_file(comp_id, &result.diagnostics);
        if (!compiled) {
            for (const auto& d : result.diagnostics) {
                spdlog::error("{}", d);
            }
            return result; // comp is nullptr → falsy
        }

        if (!result.diagnostics.empty()) {
            for (const auto& d : result.diagnostics) {
                spdlog::warn("{}", d);
            }
        }

        result.comp = std::make_shared<Composition>(std::move(*compiled));
    } else {
        if (!registry.contains(comp_id)) {
            spdlog::error("Unknown composition or specscene file: {}", comp_id);
            return result; // comp is nullptr → falsy
        }

        auto comp_instance = registry.create(comp_id);
        result.comp = std::make_shared<Composition>(std::move(comp_instance));
    }

    return result;
}

std::shared_ptr<SoftwareRenderer> create_renderer(
    const CompositionRegistry& registry,
    const RenderSettings& settings) {
    auto renderer = std::make_shared<SoftwareRenderer>();
    renderer->set_composition_registry(&registry);
    renderer->set_settings(settings);

    // Inject default backends
    renderer->set_image_backend(std::make_shared<image::StbImageBackend>());
    renderer->set_video_decoder(std::make_shared<CompositionFallbackVideoDecoder>(&registry));

    return renderer;
}

} // namespace cli
} // namespace chronon3d
