#pragma once

#include <chronon3d/core/memory/framebuffer.hpp>
#include <chronon3d/color/video_scopes.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace chronon3d::cli::scope_detail {

using json = nlohmann::json;

[[nodiscard]] bool supported_sdr_image(const std::string& path);
[[nodiscard]] bool supported_scope_image(const std::string& path);
[[nodiscard]] bool is_exr_input(const std::string& path);
[[nodiscard]] bool is_video_input(const std::string& path);
[[nodiscard]] std::shared_ptr<Framebuffer> load_scope_image(const std::string& path);
[[nodiscard]] json scope_frame_report(
    const Framebuffer& framebuffer, const std::string& input,
    bool exr_input, const std::string& input_transfer,
    std::optional<std::int64_t> render_frame_index,
    color::ScopeEncoding scope_encoding, color::ScopePrimaries scope_primaries,
    std::uint32_t waveform_width, std::uint32_t waveform_height,
    std::uint32_t vectorscope_size, color::WaveformSignal waveform_signal,
    float signal_min, float signal_max,
    json source_metadata = json::object());
[[nodiscard]] std::pair<double, double> measure_pq_luminance_nits(
    const Framebuffer& framebuffer);

} // namespace chronon3d::cli::scope_detail
