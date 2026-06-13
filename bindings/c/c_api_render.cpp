#include "c_api_internal.hpp"

#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

#include <memory>

// ── Render and save ────────────────────────────────────────────────────
chronon_status c_api_render_and_save(
    chronon_context* ctx,
    chronon3d::Composition& comp,
    const chronon_render_options* options,
    const char* output_png_path)
{
    chronon3d::SoftwareRenderer renderer;
    chronon3d::Frame target_frame = options ? options->frame : 0;

    std::shared_ptr<chronon3d::Framebuffer> fb;
    if (options && options->width > 0 && options->height > 0) {
        auto scene = comp.evaluate(target_frame);
        fb = renderer.render_scene(scene, comp.camera, options->width, options->height);
    } else {
        fb = renderer.render_frame(comp, target_frame);
    }

    if (!fb) {
        ctx->last_error = "Renderer returned null framebuffer";
        return CHRONON_ERROR_RENDER_FAILED;
    }

    if (!chronon3d::save_png(*fb, output_png_path)) {
        ctx->last_error = "Failed to save output PNG image";
        return CHRONON_ERROR_IO_FAILED;
    }

    return CHRONON_OK;
}
