// nv12_test_vectors.hpp — deterministic NV12 equivalence corpus.
//
// Wave 4 (NV12 equivalence harness) — one shared corpus of identical
// composite requests executed through BOTH NV12 authorities:
//   * Authority A — backends::vulkan::CudaNv12SurfaceCompositor
//   * Authority B — media::CudaDirectNv12Compositor
//
// Every vector is generated deterministically (xorshift64 seeded per case),
// so A/B comparisons are reproducible across hosts and runs.

#pragma once

#include <chronon3d/media/video/cuda_layer_resource.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace chronon3d::nv12_equiv {

// One authored overlay in the shared request language (before lowering to the
// compositor-specific host-layer struct).
struct OverlaySpec {
    std::uint32_t resource_width{1};
    std::uint32_t resource_height{1};
    CudaLayerResourceKind kind{CudaLayerResourceKind::Rgba};
    float src_x0{0.0f}, src_y0{0.0f}, src_x1{1.0f}, src_y1{1.0f};  // normalized
    float dst_x0{0.0f}, dst_y0{0.0f}, dst_x1{1.0f}, dst_y1{1.0f};  // canvas px
    float opacity{1.0f};
    int blend_mode{0};  // 0 = Normal, 1 = Add
};

struct Nv12Case {
    enum class Expect {
        BitExactPassthrough,   // no foreground contribution: A == B == bg, bit-exact
        RangePolicyDiff,       // fg contributes: outputs must match within the documented
                               // limited-vs-full-range conversion bound, and must DIFFER
                               // (the bound proves drift is bounded, not absent)
        MediaOnlyCapability,   // NV12-kind overlay layer: B composites, A rejects (returns false)
    };

    std::string name;
    int width{0};
    int height{0};
    std::vector<OverlaySpec> overlays;
    Expect expect{Expect::RangePolicyDiff};
    std::string rationale;  // documented deliberate difference / contract note
};

struct HostNv12Planes {
    std::vector<std::uint8_t> y;   // width * height
    std::vector<std::uint8_t> uv;  // width * ((height + 1) / 2)
};

// float4 pixels, contiguous (pitch = width * 16).
using HostRgba = std::vector<float>;

// Legal limited-range background: Y in [16, 235], Cb/Cr in [16, 240].  Kept
// inside the legal range so pure bg-passthrough pixels are bit-identical
// between the two range policies and only true fg contribution differs.
HostNv12Planes make_background_nv12(int width, int height, std::uint64_t seed);

// Strongly chromatic straight-alpha float4 content (never achromatic, so the
// chroma conversion difference is observable).  `alpha_mode` varies the alpha
// field: 0 = mostly opaque with hard transparent edge columns/rows,
// 1 = 50% flat, 2 = horizontal alpha gradient.
HostRgba make_rgba_overlay(int width, int height, std::uint64_t seed, int alpha_mode);

HostNv12Planes make_nv12_overlay(int width, int height, std::uint64_t seed);

std::vector<Nv12Case> nv12_equivalence_corpus();

} // namespace chronon3d::nv12_equiv
