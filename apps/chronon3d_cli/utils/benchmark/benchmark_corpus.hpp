#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace chronon3d::cli::benchmark {

struct CorpusCase {
    std::string_view id;
    std::string_view description;
    bool native_path;
};

/// Stable names for the hot-path benchmark corpus. The saturation command
/// remains the single runner; these identifiers make reports comparable
/// across runs without creating a second telemetry system.
inline constexpr std::array<CorpusCase, 16> kHotPathCorpus{{
    {"B00", "renderer-null", false},
    {"B01", "NVENC-only-native", true},
    {"B02", "NVDEC-only-native", true},
    {"B03", "NV12-compositor-1-layer", true},
    {"B04", "NV12-compositor-100-layers", true},
    {"B05", "MTSDF-1-title", true},
    {"B06", "MTSDF-100-text-runs", true},
    {"B07", "sparse-overlay-5", true},
    {"B08", "sparse-overlay-25", true},
    {"B09", "full-YUV", true},
    {"B10", "full-RGB-effects", true},
    {"B11", "E2E-native-single-NVENC", true},
    {"B12", "E2E-native-dual-NVENC", true},
    {"B13", "GOP-smart-copy-90", true},
    {"B14", "SIMO-3-outputs", true},
    {"B15", "multi-GPU-2-devices", true},
}};

} // namespace chronon3d::cli::benchmark
