#pragma once

#ifdef CHRONON3D_ENABLE_OCIO

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace chronon3d::color {

/// Runtime colour-space transform backed by an OpenColorIO configuration.
///
/// The renderer remains linear-sRGB by default.  This adapter is opt-in and
/// owns only the generic colour-management operation; Chronon still owns the
/// policy deciding which input/output spaces are used.
class OcioColorTransform final {
public:
    ~OcioColorTransform();
    OcioColorTransform(OcioColorTransform&&) noexcept;
    OcioColorTransform& operator=(OcioColorTransform&&) noexcept;

    OcioColorTransform(const OcioColorTransform&) = delete;
    OcioColorTransform& operator=(const OcioColorTransform&) = delete;

    /// Loads a transform from an OCIO config file and named colour spaces.
    /// Returns nullopt and writes a diagnostic when OCIO rejects the config.
    static std::optional<OcioColorTransform> from_config(
        std::string_view config_path,
        std::string_view input_space,
        std::string_view output_space,
        std::string* error = nullptr);

    /// Applies the transform in-place to one RGB triplet.
    [[nodiscard]] bool apply_rgb(float rgb[3]) const noexcept;

private:
    struct Impl;
    explicit OcioColorTransform(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronon3d::color

#endif // CHRONON3D_ENABLE_OCIO
