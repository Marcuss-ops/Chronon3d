#include "../../cli_context.hpp"

#ifdef CHRONON3D_ENABLE_OCIO

#include <chronon3d/color/ocio_color_transform.hpp>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace chronon3d::cli {
namespace {

struct OcioBakeLutOptions {
    std::string config;
    std::string working_space;
    std::string look_name;
    std::string input_space;
    std::string output_space;
    std::string output;
    int size{33};
    float domain_min{0.0f};
    float domain_max{1.0f};
};

void bake_lut(const OcioBakeLutOptions& options) {
    if (!std::isfinite(options.domain_min) || !std::isfinite(options.domain_max) ||
        options.domain_max <= options.domain_min)
        throw std::runtime_error("LUT domain must be finite and domain-max must exceed domain-min");
    if (options.size < 2 || options.size > 48)
        throw std::runtime_error("LUT size must be between 2 and 48 (the RenderPlan parser limit is 8 MiB)");

    const bool bake_look = !options.look_name.empty();
    if (bake_look) {
        if (options.working_space.empty() || !options.input_space.empty() ||
            !options.output_space.empty())
            throw std::runtime_error(
                "look mode requires --working-space and --look, and cannot use --input-space/--output-space");
    } else if (options.input_space.empty() || options.output_space.empty() ||
               !options.working_space.empty()) {
        throw std::runtime_error(
            "colorspace mode requires --input-space and --output-space, and cannot use --working-space/--look");
    }

    std::string diagnostic;
    auto look_transform = bake_look
        ? color::OcioColorTransform::from_look(
              options.config, options.working_space, options.look_name, &diagnostic)
        : std::optional<color::OcioColorTransform>{};
    auto space_transform = bake_look
        ? std::optional<color::OcioColorTransform>{}
        : color::OcioColorTransform::from_config(
              options.config, options.input_space, options.output_space, &diagnostic);
    const color::OcioColorTransform* transform = bake_look
        ? (look_transform ? &*look_transform : nullptr)
        : (space_transform ? &*space_transform : nullptr);
    if (transform == nullptr)
        throw std::runtime_error("OCIO could not build the requested transform: " + diagnostic);

    const std::filesystem::path output_path(options.output);
    if (output_path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(output_path.parent_path(), error);
        if (error)
            throw std::runtime_error("could not create LUT output directory: " + error.message());
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path temporary_path = output_path.string() + ".tmp-" +
        std::to_string(nonce);
    try {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not open temporary LUT output");
        output << "LUT_3D_SIZE " << options.size << '\n'
               << std::setprecision(std::numeric_limits<float>::max_digits10)
               << "DOMAIN_MIN " << options.domain_min << ' ' << options.domain_min << ' '
               << options.domain_min << '\n'
               << "DOMAIN_MAX " << options.domain_max << ' ' << options.domain_max << ' '
               << options.domain_max << '\n';

        const float denominator = static_cast<float>(options.size - 1);
        for (int blue = 0; blue < options.size; ++blue) {
            for (int green = 0; green < options.size; ++green) {
                for (int red = 0; red < options.size; ++red) {
                    float rgb[3]{
                        std::lerp(options.domain_min, options.domain_max,
                                  static_cast<float>(red) / denominator),
                        std::lerp(options.domain_min, options.domain_max,
                                  static_cast<float>(green) / denominator),
                        std::lerp(options.domain_min, options.domain_max,
                                  static_cast<float>(blue) / denominator),
                    };
                    if (!transform->apply_rgb(rgb) || !std::isfinite(rgb[0]) ||
                        !std::isfinite(rgb[1]) || !std::isfinite(rgb[2]))
                        throw std::runtime_error("OCIO transform failed or produced non-finite LUT samples");
                    output << rgb[0] << ' ' << rgb[1] << ' ' << rgb[2] << '\n';
                }
            }
        }
        output.close();
        if (!output) throw std::runtime_error("failed while writing OCIO LUT samples");

        std::error_code error;
        std::filesystem::rename(temporary_path, output_path, error);
        if (error)
            throw std::runtime_error("could not publish LUT output: " + error.message());
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_path, ignored);
        throw;
    }
}

} // namespace

void register_ocio_bake_lut_command(CLI::App& app, CliContext& ctx) {
    auto options = std::make_shared<OcioBakeLutOptions>();
    auto* command = app.add_subcommand(
        "ocio-bake-lut",
        "Bake an OCIO colorspace transform or scene look into a RenderPlan-compatible 3D .cube LUT");
    command->add_option("--config", options->config, "OCIO config file")
        ->required()->check(CLI::ExistingFile);
    command->add_option("--working-space", options->working_space,
                        "Working-space name for --look mode");
    command->add_option("--look", options->look_name,
                        "Named scene-referred look from the OCIO config (look mode)");
    command->add_option("--input-space", options->input_space,
                        "Input color-space name from the OCIO config (colorspace mode)");
    command->add_option("--output-space", options->output_space,
                        "Output color-space name from the OCIO config (colorspace mode)");
    command->add_option("--output", options->output, "Destination .cube file")
        ->required();
    command->add_option("--size", options->size, "LUT edge size (2-48)")
        ->check(CLI::Range(2, 48));
    command->add_option("--domain-min", options->domain_min,
                        "Shared RGB input-domain minimum (default: 0)");
    command->add_option("--domain-max", options->domain_max,
                        "Shared RGB input-domain maximum (default: 1; raise for HDR)");
    command->callback([options, &ctx]() {
        try {
            bake_lut(*options);
            spdlog::info("Baked OCIO 3D LUT to {} ({}^3, domain {}..{})",
                         options->output, options->size, options->domain_min,
                         options->domain_max);
        } catch (const std::exception& error) {
            spdlog::error("OCIO LUT bake failed: {}", error.what());
            ctx.exit_code = 1;
        }
    });
}

} // namespace chronon3d::cli

#endif // CHRONON3D_ENABLE_OCIO
