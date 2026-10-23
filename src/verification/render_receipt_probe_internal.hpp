#pragma once

#include <chronon3d/verification/render_receipt.hpp>

#include <filesystem>

namespace chronon3d::verification::receipt_probe {
bool probe_media(const std::filesystem::path& output, MediaContract& media,
                 bool require_exit_ok, double* milliseconds);
std::int64_t count_frames_ffprobe(const std::filesystem::path& output,
                                  double* milliseconds);
bool mastering_metadata_matches(
    const color::HdrMasteringMetadata& expected,
    const std::optional<color::HdrMasteringMetadata>& observed);
} // namespace chronon3d::verification::receipt_probe
