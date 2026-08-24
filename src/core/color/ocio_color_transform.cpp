#ifdef CHRONON3D_ENABLE_OCIO

#include <chronon3d/color/ocio_color_transform.hpp>

#include <OpenColorIO/OpenColorIO.h>

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

bool OcioColorTransform::apply_rgb(float rgb[3]) const noexcept {
    if (impl_ == nullptr || impl_->processor == nullptr || rgb == nullptr) return false;
    try {
        impl_->processor->applyRGB(rgb);
        return true;
    } catch (const OCIO::Exception&) {
        return false;
    }
}

} // namespace chronon3d::color

#endif // CHRONON3D_ENABLE_OCIO
