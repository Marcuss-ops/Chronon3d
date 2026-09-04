bool save_image(const Framebuffer& framebuffer,
                const std::string& path,
                const ImageWriteOptions& options) {
    CHRONON_TRACE_SCOPE("chronon.io", "save_image");
    ImageFormat format = options.format;

    if (format == ImageFormat::Unknown) {
        format = image_format_from_path(path);
    }

    switch (format) {
        case ImageFormat::Png:
            // TICKET-RENDER-PIPELINE-INTEGRITY layer 2 (M1 hardening):
            // convert the save_png per-pixel-NaN/Inf throw into a clean
            // `return false` so the caller (write_frame_to_disk) can
            // surface its existing "Failed to save frame N to path ..."
            // error log instead of std::terminate-ing the CLI main loop.
            try {
                return save_png(framebuffer, path);
            } catch (const std::exception& e) {
                spdlog::error(
                    "save_image: save_png threw on path '{}': {} — "
                    "returning false to caller",
                    path, e.what());
                return false;
            } catch (...) {
                spdlog::error(
                    "save_image: save_png threw an unknown exception on "
                    "path '{}' — returning false to caller", path);
                return false;
            }

        case ImageFormat::Exr:
            // save_exr already wraps its main work in try/catch and
            // returns false on std::exception, so no extra wrapping
            // here is needed for that path.
            return save_exr(framebuffer, path, options);

        default:
            return false;
    }
}
