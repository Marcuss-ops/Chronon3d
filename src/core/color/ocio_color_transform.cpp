#ifdef CHRONON3D_ENABLE_OCIO

#include <chronon3d/color/ocio_color_transform.hpp>
#include <chronon3d/core/memory/framebuffer.hpp>

#include <OpenColorIO/OpenColorIO.h>
#include <OpenColorIO/OpenColorTransforms.h>

#include <cmath>
#include <vector>
#include <utility>

namespace OCIO = OCIO_NAMESPACE;

namespace chronon3d::color {

struct OcioColorTransform::Impl {
    OCIO::ConstCPUProcessorRcPtr processor;
};

OcioColorTransform::OcioColorTransform(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

OcioColorTransform::~OcioColorTransform() = default;
OcioColorTransform::OcioColorTransform(OcioColorTransform&&) noexcept = default;
OcioColorTransform& OcioColorTransform::operator=(OcioColorTransform&&) noexcept = default;

std::optional<OcioColorTransform> OcioColorTransform::from_config(
    std::string_view config_path,
    std::string_view input_space,
    std::string_view output_space,
    std::string* error)
{
    try {
        const auto config = OCIO::Config::CreateFromFile(std::string(config_path).c_str());
        const auto processor = config->getProcessor(
            std::string(input_space).c_str(), std::string(output_space).c_str());
        auto impl = std::make_unique<Impl>();
        impl->processor = processor->getDefaultCPUProcessor();
        return OcioColorTransform(std::move(impl));
    } catch (const OCIO::Exception& exception) {
        if (error != nullptr) *error = exception.what();
        return std::nullopt;
    }
}

std::optional<OcioColorTransform> OcioColorTransform::from_display_view(
    std::string_view config_path,
    std::string_view source_space,
    std::string_view display,
    std::string_view view,
    std::string* error)
{
    try {
        const auto config = OCIO::Config::CreateFromFile(std::string(config_path).c_str());
        const auto source = std::string(source_space);
        const auto display_name = std::string(display);
        const auto view_name = std::string(view);
        const auto processor = config->getProcessor(
            source.c_str(), display_name.c_str(), view_name.c_str(),
            OCIO::TRANSFORM_DIR_FORWARD);
        auto impl = std::make_unique<Impl>();
        impl->processor = processor->getDefaultCPUProcessor();
        return OcioColorTransform(std::move(impl));
    } catch (const OCIO::Exception& exception) {
        if (error != nullptr) *error = exception.what();
        return std::nullopt;
    }
}

std::optional<OcioColorTransform> OcioColorTransform::from_look(
    std::string_view config_path,
    std::string_view working_space,
    std::string_view look_name,
    std::string* error)
{
    try {
        const auto config = OCIO::Config::CreateFromFile(std::string(config_path).c_str());
        auto look = OCIO::LookTransform::Create();
        const auto space = std::string(working_space);
        const auto name = std::string(look_name);
        look->setSrc(space.c_str());
        look->setDst(space.c_str());
        look->setLooks(name.c_str());
        const auto processor = config->getProcessor(look);
        auto impl = std::make_unique<Impl>();
        impl->processor = processor->getDefaultCPUProcessor();
        return OcioColorTransform(std::move(impl));
    } catch (const OCIO::Exception& exception) {
        if (error != nullptr) *error = exception.what();
        return std::nullopt;
    }
}

bool OcioColorTransform::apply_rgb(float rgb[3]) const noexcept {
    if (impl_ == nullptr || impl_->processor == nullptr || rgb == nullptr) return false;
    try {
        impl_->processor->applyRGB(rgb);
        return true;
    } catch (const OCIO::Exception&) {
        return false;
    }
}

bool OcioColorTransform::apply_framebuffer(Framebuffer& framebuffer) const noexcept {
    if (impl_ == nullptr || impl_->processor == nullptr) return false;
    try {
        const auto width = static_cast<std::size_t>(framebuffer.width());
        const auto height = static_cast<std::size_t>(framebuffer.height());
        if (width != 0 && height > std::vector<Color>{}.max_size() / width)
            return false;

        // Stage the full visible image so an invalid pixel, OCIO error, or
        // allocation failure cannot leave the caller's frame half transformed.
        std::vector<Color> transformed(width * height);
        for (std::size_t y = 0; y < height; ++y) {
            const Color* row = framebuffer.pixels_row(static_cast<int>(y));
            for (std::size_t x = 0; x < width; ++x) {
                const Color& pixel = row[x];
                const float alpha = pixel.a;
                if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f ||
                    !std::isfinite(pixel.r) || !std::isfinite(pixel.g) ||
                    !std::isfinite(pixel.b))
                    return false;

                Color result = pixel;
                if (alpha > 0.0f) {
                    const float inverse_alpha = 1.0f / alpha;
                    float rgb[3]{pixel.r * inverse_alpha, pixel.g * inverse_alpha,
                                 pixel.b * inverse_alpha};
                    if (!apply_rgb(rgb) || !std::isfinite(rgb[0]) ||
                        !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]))
                        return false;
                    result.r = rgb[0] * alpha;
                    result.g = rgb[1] * alpha;
                    result.b = rgb[2] * alpha;
                    if (!std::isfinite(result.r) || !std::isfinite(result.g) ||
                        !std::isfinite(result.b))
                        return false;
                }
                transformed[y * width + x] = result;
            }
        }

        for (std::size_t y = 0; y < height; ++y) {
            Color* row = framebuffer.pixels_row(static_cast<int>(y));
            for (std::size_t x = 0; x < width; ++x)
                row[x] = transformed[y * width + x];
        }
        return true;
    } catch (...) {
        // The method is noexcept; preserve the strong failure guarantee for
        // both allocation failures and exceptions from OCIO.
        return false;
    }
}

} // namespace chronon3d::color

#endif // CHRONON3D_ENABLE_OCIO
