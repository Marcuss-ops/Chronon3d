#pragma once

#include "../../cli_context.hpp"
#include <chronon3d/core/memory/framebuffer.hpp>
#include <CLI/CLI.hpp>
#include <nlohmann/json_fwd.hpp>
#include "command_mask_propagate_state.hpp"
#include <cstddef>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace chronon3d::cli {

void register_mask_segment_command(CLI::App& app, CliContext& ctx);

namespace mask_propagation_detail {
void decode_sdr_video_frames(MaskPropagationState&, const TemporaryMaskSpool&);
[[nodiscard]] bool has_plan_patch_options(const MaskPropagationState&);
void validate_plan_patch_option_set(const MaskPropagationState&);
void attach_matte_video_to_render_plan(const MaskPropagationState&);

[[nodiscard]] std::string output_path(const std::string&, std::size_t, const std::string& suffix = {});
[[nodiscard]] nlohmann::json encode_propagated_matte_video(
    const MaskPropagationState&, std::uint32_t, std::uint32_t);
void append_encoded_video_verification(const MaskPropagationState&, const nlohmann::json&);

[[nodiscard]] std::shared_ptr<Framebuffer> load_frame(const std::string& path);
[[nodiscard]] std::shared_ptr<Framebuffer> load_tracking_frame(
    const std::string& path, const std::string& transfer);
[[nodiscard]] bool write_gray(const std::string& path,
                              std::span<const float> values,
                              std::uint32_t width, std::uint32_t height);
} // namespace mask_propagation_detail

} // namespace chronon3d::cli
