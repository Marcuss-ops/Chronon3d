// nv12_test_vectors.hpp — deterministic NV12 compositor contract corpus.
//
// Wave 4 (NV12 equivalence harness) was originally authored against two
// coexisting NV12 authorities. Wave 1D unified them into the single
// media::video::CudaNv12Compositor authority under
// src/media/video/compositor/.  The corpus below survives as the shared,
// deterministic regression matrix for that unified compositor: every request
// is executed through two INDEPENDENT compositor instances and must be
// bit-exact across instances and re-runs (the post-unification determinism
// contract), with documented per-case outcome classes.
//
// Every vector is generated deterministically (xorshift64 seeded per case),
// so comparisons are reproducible across hosts and runs.

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
        DeterministicComposite,  // foreground contributes: both independent
                                 // instances succeed and must be bit-exact
        BitExactPassthrough,     // zero foreground contribution: both succeed,
                                 // outputs bit-exact AND equal to the background
        RejectedLayer,           // layer outside the direct-batch contract
                                 // (e.g. NV12-kind resource, unsupported
                                 // blend): both instances must reject
    };

    std::string name;
    int width{0};
    int height{0};
    std::vector<OverlaySpec> overlays;
    Expect expect{Expect::DeterministicComposite};
    std::string rationale;  // documented contract note
};

struct HostNv12Planes {
    std::vector<std::uint8_t> y;   // width * height
    std::vector<std::uint8_t> uv;  // width * ((height + 1) / 2)
};

// float4 pixels, contiguous (pitch = width * 16).
using HostRgba = std::vector<float>;

// Legal limited-range background: Y in [16, 235], Cb/Cr in [16, 240].
HostNv12Planes make_background_nv12(int width, int height, std::uint64_t seed);

// Strongly chromatic straight-alpha float4 content (never achromatic, so the
// chroma conversion difference is observable).  `alpha_mode` varies the alpha
// field: 0 = mostly opaque with hard transparent edge columns/rows,
// 1 = 50% flat, 2 = horizontal alpha gradient.
HostRgba make_rgba_overlay(int width, int height, std::uint64_t seed, int alpha_mode);

HostNv12Planes make_nv12_overlay(int width, int height, std::uint64_t seed);

std::vector<Nv12Case> nv12_equivalence_corpus();

} // namespace chronon3d::nv12_equiv
