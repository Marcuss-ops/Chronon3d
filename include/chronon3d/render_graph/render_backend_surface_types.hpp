#pragma once

#include <array>
#include <cstdint>

namespace chronon3d::graph {

/// CPU-side parameter layouts shared by native surface operations.
/// Keep these independent from the RenderBackend class declaration so
/// surface-operation users can include only the data contract they need.
struct NativeColorTransform {
    enum class Mode : std::uint8_t {
        Affine = 0, Levels = 1, Threshold = 2, Keying = 3, Vignette = 4,
        Mosaic = 5, LiftGammaGain = 6, Offset = 7, Noise = 8, Checkerboard = 9,
        ColorMatrix = 10
    };
    Mode mode{Mode::Affine};
    std::array<std::array<float, 4>, 5> p{{
        {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}}};
    bool clamp_unit{false};
    bool straight_alpha{false};
};

/// Push layout is 128 bytes: mode, edge mode, and 30 float parameters.
struct WarpBulkParams {
    int mode{0};
    int edge_mode{0};
    float p[30]{};
};
static_assert(sizeof(WarpBulkParams) == 128, "WarpBulkParams must be 128 bytes");

/// Push layout is 128 bytes and matches the native generate shader block.
struct GenerateBulkParams {
    int mode{0};
    int flags{0};
    int _pad[2]{0, 0};
    float a[16]{};
    float b[12]{};
};
static_assert(sizeof(GenerateBulkParams) == 128, "GenerateBulkParams must be 128 bytes");

/// Push layout is 64 bytes and matches the native LUT shader block.
struct LutBulkParams {
    int mode{0};
    float amount{1.0f};
    float pad0{0.0f};
    float pad1{0.0f};
    float dmin[4]{0.0f, 0.0f, 0.0f, 0.0f};
    float dmax[4]{1.0f, 1.0f, 1.0f, 0.0f};
    int meta[4]{0, 0, 0, 0};
};
static_assert(sizeof(LutBulkParams) == 64, "LutBulkParams must be 64 bytes");

} // namespace chronon3d::graph
