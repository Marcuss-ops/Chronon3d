// nv12_equivalence_test.cpp — NV12 compositor determinism + contract matrix.
//
// Wave 4 originally compared TWO coexisting NV12 authorities (Vulkan-side
// CudaNv12SurfaceCompositor vs media-side CudaDirectNv12Compositor) to
// produce equivalence evidence before unification.  Wave 1D unified them:
// media::video::CudaNv12Compositor under src/media/video/compositor/ is now
// the single authority.  This harness therefore runs the SAME request through
// two INDEPENDENT instances of that unified compositor and asserts the
// retained equivalence property — bit-exact determinism — plus the documented
// per-case contract of the direct NV12 batch path:
//
//   * DeterministicComposite — foreground contributes: both instances
//     succeed and their planes are bit-identical.
//   * BitExactPassthrough    — zero foreground contribution: both succeed,
//     outputs are bit-identical AND equal to the background input.
//   * RejectedLayer          — layer outside the direct-batch contract
//     (e.g. NV12-kind overlay resource): both instances reject (false).
//
// Comparison per plane (Y = w*h bytes; UV = w * ceil(h/2) bytes, interleaved):
//   * bit-exact equality
//   * differing-byte count
//   * max absolute difference
//   * PSNR (dB, infinite when identical)
//
// Matrix lines are printed unconditionally so GPU-host runs produce the
// determinism/contract evidence the audit requires.
//
// Runtime opt-in (same convention as the P010 conversion tests):
//   CHRONON3D_RUN_CUDA_TESTS=1 <binary>

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP

#include "nv12_test_vectors.hpp"

#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#include <chronon3d/runtime/gpu_layer_batch.hpp>

#include <cuda.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d::media::video::cuda_nv12_detail {
std::string get_compiled_nv12_ptx();
}

namespace {

bool gpu_tests_enabled() {
    const char* value = std::getenv("CHRONON3D_RUN_CUDA_TESTS");
    return value && std::string(value) == "1";
}

void check_ok(CUresult result, const char* what) {
    if (result != CUDA_SUCCESS) {
        const char* name = nullptr;
        const char* desc = nullptr;
        cuGetErrorName(result, &name);
        cuGetErrorString(result, &desc);
        std::string detail = name ? name : "CUDA error";
        if (desc) detail += std::string(" (") + desc + ")";
        FAIL(what << ": " << detail);
    }
}

struct DevBuffer {
    CUdeviceptr ptr{0};
    std::size_t bytes{0};

    DevBuffer() = default;
    ~DevBuffer() {
        if (ptr) (void)cuMemFree(ptr);
    }
    DevBuffer(const DevBuffer&) = delete;
    DevBuffer& operator=(const DevBuffer&) = delete;
    DevBuffer(DevBuffer&& other) noexcept
        : ptr(other.ptr), bytes(other.bytes) {
        other.ptr = 0;
        other.bytes = 0;
    }
    DevBuffer& operator=(DevBuffer&& other) noexcept {
        if (this == &other) return *this;
        if (ptr) (void)cuMemFree(ptr);
        ptr = other.ptr;
        bytes = other.bytes;
        other.ptr = 0;
        other.bytes = 0;
        return *this;
    }

    void alloc(std::size_t size) {
        bytes = size;
        check_ok(cuMemAlloc(&ptr, bytes), "cuMemAlloc");
    }
    void upload_async(CUstream stream, const void* host) const {
        check_ok(cuMemcpyHtoDAsync(ptr, host, bytes, stream), "cuMemcpyHtoDAsync");
    }
    void download(void* host) const {
        check_ok(cuMemcpyDtoH(host, ptr, bytes), "cuMemcpyDtoH");
    }
};

struct CudaSession {
    CUdevice device{0};
    CUcontext context{nullptr};
    CUstream stream{nullptr};
    bool ok{false};

    CudaSession() {
        if (cuInit(0) != CUDA_SUCCESS) return;
        if (cuDeviceGet(&device, 0) != CUDA_SUCCESS) return;
#if defined(CUDA_VERSION) && CUDA_VERSION >= 13000
        if (cuCtxCreate(&context, nullptr, 0, device) != CUDA_SUCCESS) return;
#else
        if (cuCtxCreate(&context, 0, device) != CUDA_SUCCESS) return;
#endif
        if (cuStreamCreate(&stream, CU_STREAM_NON_BLOCKING) != CUDA_SUCCESS) return;
        ok = true;
    }
    ~CudaSession() {
        if (!ok) return;
        (void)cuCtxSetCurrent(context);
        if (stream) (void)cuStreamDestroy(stream);
        if (context) (void)cuCtxDestroy(context);
    }
    void set_current() { check_ok(cuCtxSetCurrent(context), "cuCtxSetCurrent"); }
};

TEST_CASE("NV12 PTX ProgramCache artifact has stable identity") {
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run NV12 PTX cache test");
        return;
    }
    CudaSession session;
    if (!session.ok) {
        MESSAGE("CUDA device/context unavailable for NV12 PTX cache test");
        return;
    }
    session.set_current();
    chronon3d::media::video::CudaNv12Compositor::warm_up(session.context);
    const auto first = chronon3d::media::video::cuda_nv12_detail::get_compiled_nv12_ptx();
    const auto second = chronon3d::media::video::cuda_nv12_detail::get_compiled_nv12_ptx();
    REQUIRE_FALSE(first.empty());
    CHECK(first == second);
}

struct PlaneMetrics {
    bool identical{true};
    std::size_t differing{0};
    std::size_t total{0};
    double max_abs{0.0};
    double psnr_db{0.0};
};

PlaneMetrics compare_plane(const std::uint8_t* a, const std::uint8_t* b, std::size_t bytes) {
    PlaneMetrics m;
    m.total = bytes;
    double ssd = 0.0;
    for (std::size_t i = 0; i < bytes; ++i) {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        if (d != 0) {
            m.identical = false;
            ++m.differing;
        }
        const double ad = static_cast<double>(d < 0 ? -d : d);
        ssd += ad * ad;
        if (ad > m.max_abs) m.max_abs = ad;
    }
    if (m.identical) {
        m.psnr_db = std::numeric_limits<double>::infinity();
    } else {
        const double mse = ssd / static_cast<double>(bytes);
        m.psnr_db = 10.0 * std::log10((255.0 * 255.0) / mse);
    }
    return m;
}

struct CaseMetrics {
    std::string name;
    bool ok_a{false};
    bool ok_b{false};
    PlaneMetrics y;
    PlaneMetrics uv;
    PlaneMetrics y_vs_bg;   // instance A output vs the background input
    PlaneMetrics uv_vs_bg;  // (BitExactPassthrough contract only)
    int expected{0};  // static_cast<int>(Nv12Case::Expect)
};

// One full request lowered to both independent compositor instances.  Returns
// metrics + PASS/FAIL per the corpus expectation.  bg/out device buffers are
// owned by the caller.
void run_case(const CudaSession& session,
              chronon3d::media::video::CudaNv12Compositor& comp_a,
              chronon3d::media::video::CudaNv12Compositor& comp_b,
              const chronon3d::nv12_equiv::Nv12Case& test_case,
              const std::vector<std::uint8_t>& bg_y_host,
              const std::vector<std::uint8_t>& bg_uv_host,
              const std::vector<chronon3d::nv12_equiv::HostRgba>& rgba_overlays,
              const std::vector<chronon3d::nv12_equiv::HostNv12Planes>& nv12_overlays,
              CaseMetrics& out) {
    using namespace chronon3d;
    using namespace chronon3d::nv12_equiv;
    using chronon3d::media::CudaLayerResource;
    using chronon3d::media::CudaLayerResourceKind;

    out.name = test_case.name;
    out.expected = static_cast<int>(test_case.expect);

    const int w = test_case.width;
    const int h = test_case.height;
    const std::size_t y_bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t uv_bytes = static_cast<std::size_t>(w) *
        static_cast<std::size_t>((h + 1) / 2);

    // ── device buffers ───────────────────────────────────────────────────
    DevBuffer bg_y_d, bg_uv_d;
    bg_y_d.alloc(y_bytes);
    bg_uv_d.alloc(uv_bytes);
    DevBuffer out_a_y, out_a_uv, out_b_y, out_b_uv;
    out_a_y.alloc(y_bytes);  out_a_uv.alloc(uv_bytes);
    out_b_y.alloc(y_bytes);  out_b_uv.alloc(uv_bytes);

    std::vector<DevBuffer> overlay_rgba_d;
    std::vector<DevBuffer> overlay_nv12_y_d, overlay_nv12_uv_d;
    std::vector<CudaLayerResource> resources;
    std::vector<runtime::LayerInstance> instances;

    std::size_t rgba_index = 0;
    std::size_t nv12_index = 0;
    for (const auto& overlay : test_case.overlays) {
        runtime::LayerInstance instance;
        instance.kind = runtime::PrimitiveKind::Image;
        instance.resource_index = static_cast<std::uint32_t>(resources.size());
        instance.src_x0 = overlay.src_x0;
        instance.src_y0 = overlay.src_y0;
        instance.src_x1 = overlay.src_x1;
        instance.src_y1 = overlay.src_y1;
        instance.dst_x0 = overlay.dst_x0;
        instance.dst_y0 = overlay.dst_y0;
        instance.dst_x1 = overlay.dst_x1;
        instance.dst_y1 = overlay.dst_y1;
        instance.opacity = overlay.opacity;
        instance.blend = overlay.blend_mode == 1 ? BlendMode::Add : BlendMode::Normal;
        instances.push_back(instance);

        CudaLayerResource resource;
        resource.kind = overlay.kind;
        resource.width = overlay.resource_width;
        resource.height = overlay.resource_height;
        if (overlay.kind == CudaLayerResourceKind::Rgba) {
            const std::size_t bytes = static_cast<std::size_t>(overlay.resource_width) *
                overlay.resource_height * 16;
            DevBuffer d;
            d.alloc(bytes);
            d.upload_async(session.stream, rgba_overlays[rgba_index++].data());
            resource.rgba = d.ptr;
            resource.pitch_bytes = static_cast<int>(overlay.resource_width * 16);
            overlay_rgba_d.push_back(std::move(d));
        } else {
            const std::size_t yb = static_cast<std::size_t>(overlay.resource_width) *
                overlay.resource_height;
            const std::size_t uvb = static_cast<std::size_t>(overlay.resource_width) *
                ((overlay.resource_height + 1) / 2);
            DevBuffer dy, duv;
            dy.alloc(yb);
            duv.alloc(uvb);
            dy.upload_async(session.stream, nv12_overlays[nv12_index].y.data());
            duv.upload_async(session.stream, nv12_overlays[nv12_index].uv.data());
            resource.y = dy.ptr;
            resource.uv = duv.ptr;
            resource.pitch_bytes = static_cast<int>(overlay.resource_width);
            resource.uv_pitch_bytes = static_cast<int>(overlay.resource_width);
            overlay_nv12_y_d.push_back(std::move(dy));
            overlay_nv12_uv_d.push_back(std::move(duv));
            ++nv12_index;
        }
        resources.push_back(resource);
    }

    runtime::GpuLayerBatch batch;
    batch.instances = std::move(instances);

    // ── enqueue: bg + overlays already queued above; then both compositors ──
    bg_y_d.upload_async(session.stream, bg_y_host.data());
    bg_uv_d.upload_async(session.stream, bg_uv_host.data());

    out.ok_a = comp_a.composite_direct_nv12_batch(
        batch, resources,
        bg_y_d.ptr, w, bg_uv_d.ptr, w,
        out_a_y.ptr, w, out_a_uv.ptr, w,
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), session.stream);
    out.ok_b = comp_b.composite_direct_nv12_batch(
        batch, resources,
        bg_y_d.ptr, w, bg_uv_d.ptr, w,
        out_b_y.ptr, w, out_b_uv.ptr, w,
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), session.stream);

    check_ok(cuStreamSynchronize(session.stream), "cuStreamSynchronize");

    // ── download + compare ───────────────────────────────────────────────
    std::vector<std::uint8_t> a_y(y_bytes), a_uv(uv_bytes), b_y(y_bytes), b_uv(uv_bytes);
    out_a_y.download(a_y.data());
    out_a_uv.download(a_uv.data());
    out_b_y.download(b_y.data());
    out_b_uv.download(b_uv.data());

    out.y = compare_plane(a_y.data(), b_y.data(), y_bytes);
    out.uv = compare_plane(a_uv.data(), b_uv.data(), uv_bytes);
    // Background comparison (only meaningful when a request must not
    // contribute foreground pixels; fills the passthrough contract).
    out.y_vs_bg = compare_plane(a_y.data(), bg_y_host.data(), y_bytes);
    out.uv_vs_bg = compare_plane(a_uv.data(), bg_uv_host.data(), uv_bytes);
}

void print_matrix_line(const CaseMetrics& m, bool pass) {
    std::printf("[NV12-EQ] %-40s A=%d B=%d | A-B Y: diff=%zu/%zu max=%.0f psnr=%6.2f | "
                "UV: diff=%zu/%zu max=%.0f psnr=%6.2f | expect=%d => %s\n",
                m.name.c_str(), m.ok_a ? 1 : 0, m.ok_b ? 1 : 0,
                m.y.differing, m.y.total, m.y.max_abs, m.y.psnr_db,
                m.uv.differing, m.uv.total, m.uv.max_abs, m.uv.psnr_db,
                m.expected, pass ? "PASS" : "FAIL");
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════
// Contract matrix: identical request through two independent instances of the
// unified compositor must be bit-exact, with the documented per-class
// outcome (composite / background passthrough / rejection).
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("NV12 contract matrix: independent instances, unified compositor") {
    using namespace chronon3d::nv12_equiv;
    using chronon3d::media::CudaLayerResourceKind;
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run NV12 contract tests");
        return;
    }

    CudaSession session;
    if (!session.ok) {
        MESSAGE("CUDA device/context unavailable; NV12 contract matrix is NOT RUN");
        return;
    }
    session.set_current();

    chronon3d::media::video::CudaNv12Compositor comp_a(session.context);
    chronon3d::media::video::CudaNv12Compositor comp_b(session.context);

    const auto corpus = nv12_equivalence_corpus();
    REQUIRE_FALSE(corpus.empty());

    std::printf("[NV12-EQ] contract matrix: %zu cases\n", corpus.size());
    std::size_t passed = 0;
    for (const auto& test_case : corpus) {
        // Deterministic host inputs, one seed per case.
        const auto seed = 0xC0FFEE0000000000ULL ^ static_cast<std::uint64_t>(test_case.name.size()) ^
            (static_cast<std::uint64_t>(test_case.width) << 32) ^
            static_cast<std::uint64_t>(test_case.height) ^ 0x5EEDULL;

        const auto bg = make_background_nv12(test_case.width, test_case.height, seed);
        std::vector<HostRgba> rgba_host;
        std::vector<HostNv12Planes> nv12_host;
        for (std::size_t i = 0; i < test_case.overlays.size(); ++i) {
            const auto& o = test_case.overlays[i];
            if (o.kind == CudaLayerResourceKind::Rgba) {
                rgba_host.push_back(make_rgba_overlay(
                    static_cast<int>(o.resource_width), static_cast<int>(o.resource_height),
                    seed ^ 0x1111ULL * static_cast<std::uint64_t>(i + 1), static_cast<int>(i % 3)));
            } else {
                nv12_host.push_back(make_nv12_overlay(
                    static_cast<int>(o.resource_width), static_cast<int>(o.resource_height),
                    seed ^ 0x2222ULL * static_cast<std::uint64_t>(i + 1)));
            }
        }

        CaseMetrics metrics;
        run_case(session, comp_a, comp_b, test_case, bg.y, bg.uv, rgba_host, nv12_host, metrics);

        bool pass = true;
        if (test_case.expect == Nv12Case::Expect::BitExactPassthrough) {
            // Zero foreground contribution: both succeed, instances agree
            // bit-exactly AND the output equals the background input.
            const bool passthrough =
                metrics.y_vs_bg.identical && metrics.uv_vs_bg.identical;
            if (!(metrics.ok_a && metrics.ok_b &&
                  metrics.y.identical && metrics.uv.identical && passthrough)) {
                pass = false;
            }
            CHECK(metrics.ok_a);
            CHECK(metrics.ok_b);
            CHECK(metrics.y.identical);
            CHECK(metrics.uv.identical);
            CHECK(passthrough);
        } else if (test_case.expect == Nv12Case::Expect::DeterministicComposite) {
            // Foreground contributes: both succeed and must be bit-exact.
            if (!(metrics.ok_a && metrics.ok_b &&
                  metrics.y.identical && metrics.uv.identical)) {
                pass = false;
            }
            CHECK(metrics.ok_a);
            CHECK(metrics.ok_b);
            CHECK(metrics.y.identical);
            CHECK(metrics.uv.identical);
        } else {  // RejectedLayer — outside the direct-batch contract
            if (metrics.ok_a || metrics.ok_b) {
                pass = false;
            }
            CHECK_FALSE(metrics.ok_a);
            CHECK_FALSE(metrics.ok_b);
        }

        print_matrix_line(metrics, pass);
        if (pass) ++passed;
    }
    std::printf("[NV12-EQ] matrix result: %zu/%zu cases within documented expectations\n",
                passed, corpus.size());
    CHECK(passed == corpus.size());
}

// ═════════════════════════════════════════════════════════════════════════
// MTSDF text shadow: the canonical overlay shadow composites on the SAME
// direct NV12 pass that carries the text. The prepared distance field is
// resampled in the kernel, so the shadow needs no second rasterization, no
// RGBA intermediate and no extra per-frame asset work. This case pins the
// visible contract: the authored offset direction, the blur widening the
// silhouette coverage, and bit-exact determinism.
// ═════════════════════════════════════════════════════════════════════════
namespace {

/// Pack one MTSDF texel bit-preservingly: the alpha lane is the canonical
/// signed distance (0.5 == outline) normalized over `range_px`, exactly the
/// atlas upload ABI the CUDA kernel decodes.
std::uint32_t pack_mtsdf_texel(float signed_distance_px, float range_px) {
    const float alpha = std::clamp(0.5f + signed_distance_px / range_px, 0.0f, 1.0f);
    return static_cast<std::uint32_t>(std::lround(alpha * 255.0f)) << 24u;
}

/// Flat disc silhouette: the simplest field whose offset/blur behaviour is
/// analytically checkable.
std::vector<float> make_disc_atlas(int size, float radius, float range_px) {
    std::vector<float> atlas(static_cast<std::size_t>(size) * static_cast<std::size_t>(size),
                             0.0f);
    const float center = (static_cast<float>(size) - 1.0f) * 0.5f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x) - center;
            const float dy = static_cast<float>(y) - center;
            const float distance = radius - std::sqrt(dx * dx + dy * dy);
            const std::uint32_t packed = pack_mtsdf_texel(distance, range_px);
            std::memcpy(&atlas[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) +
                              static_cast<std::size_t>(x)],
                        &packed, sizeof(packed));
        }
    }
    return atlas;
}

int luma_at(const std::vector<std::uint8_t>& y, int width, int x, int row) {
    return static_cast<int>(y[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) +
                               static_cast<std::size_t>(x)]);
}

} // namespace

TEST_CASE("NV12 direct batch: the text drop shadow composites from the prepared field") {
    using namespace chronon3d::nv12_equiv;
    using chronon3d::media::CudaLayerResource;
    using chronon3d::media::CudaLayerResourceKind;
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run the MTSDF shadow contract test");
        return;
    }

    CudaSession session;
    if (!session.ok) {
        MESSAGE("CUDA device/context unavailable; MTSDF shadow contract is NOT RUN");
        return;
    }
    session.set_current();

    chronon3d::media::video::CudaNv12Compositor comp_a(session.context);
    chronon3d::media::video::CudaNv12Compositor comp_b(session.context);

    const int w = 640;
    const int h = 360;
    const int atlas_size = 64;
    const float range_px = 8.0f;
    const float disc_radius = 20.0f;
    const auto bg = make_background_nv12(w, h, 0x5AD0ULL);
    const auto atlas = make_disc_atlas(atlas_size, disc_radius, range_px);

    const std::size_t y_bytes = static_cast<std::size_t>(w) * h;
    const std::size_t uv_bytes = static_cast<std::size_t>(w) * ((h + 1) / 2);

    // Glyph plane placed away from the frame edges: the shadow envelope and
    // the surrounding background are both observable.
    const float dst_x0 = 288.0f;
    const float dst_y0 = 148.0f;
    const float dst_x1 = dst_x0 + static_cast<float>(atlas_size);
    const float dst_y1 = dst_y0 + static_cast<float>(atlas_size);
    // True atlas-space disc centre (the field is built around it).
    const int center_x = static_cast<int>(dst_x0 + (atlas_size - 1) * 0.5f);
    const int center_y = static_cast<int>(dst_y0 + (atlas_size - 1) * 0.5f);

    DevBuffer atlas_d, bg_y_d, bg_uv_d;
    atlas_d.alloc(atlas.size() * sizeof(float));
    atlas_d.upload_async(session.stream, atlas.data());
    bg_y_d.alloc(y_bytes);
    bg_uv_d.alloc(uv_bytes);

    chronon3d::runtime::LayerInstance instance;
    instance.kind = chronon3d::runtime::PrimitiveKind::Text;
    instance.resource_index = 0;
    instance.src_x0 = 0.0f;
    instance.src_y0 = 0.0f;
    instance.src_x1 = 1.0f;
    instance.src_y1 = 1.0f;
    instance.dst_x0 = dst_x0;
    instance.dst_y0 = dst_y0;
    instance.dst_x1 = dst_x1;
    instance.dst_y1 = dst_y1;
    instance.opacity = 1.0f;

    chronon3d::runtime::GpuLayerBatch batch;
    batch.instances = {instance};

    CudaLayerResource resource;
    resource.kind = CudaLayerResourceKind::Mtsdf;
    resource.rgba = atlas_d.ptr;
    resource.pitch_bytes = atlas_size * static_cast<int>(sizeof(std::uint32_t));
    resource.width = static_cast<std::uint32_t>(atlas_size);
    resource.height = static_cast<std::uint32_t>(atlas_size);
    resource.distance_range_px = range_px;
    resource.color = 0xFFFFFFFFu;  // opaque white fill, linear

    // Canonical overlay preset shadow: #000000 / opacity 0.72 / offset (0,4).
    const auto run_with = [&](float blur, std::vector<std::uint8_t>& out_y,
                              chronon3d::media::video::CudaNv12Compositor& compositor,
                              const std::vector<std::uint8_t>& bg_uv) {
        CudaLayerResource shaded = resource;
        if (blur >= 0.0f) {
            shaded.shadow_color = 0xFF000000u;  // opaque black, linear
            shaded.shadow_opacity = 0.72f;
            shaded.shadow_dx = 0.0f;
            shaded.shadow_dy = 4.0f;
            shaded.shadow_blur = blur;
        }
        const std::vector<CudaLayerResource> resources = {shaded};
        DevBuffer out_y_d, out_uv_d;
        out_y_d.alloc(y_bytes);
        out_uv_d.alloc(uv_bytes);
        bg_y_d.upload_async(session.stream, bg.y.data());
        bg_uv_d.upload_async(session.stream, bg_uv.data());
        REQUIRE(compositor.composite_direct_nv12_batch(
            batch, resources, bg_y_d.ptr, w, bg_uv_d.ptr, w, out_y_d.ptr, w,
            out_uv_d.ptr, w, static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), session.stream));
        check_ok(cuStreamSynchronize(session.stream), "cuStreamSynchronize");
        out_y.resize(y_bytes);
        out_y_d.download(out_y.data());
    };

    std::vector<std::uint8_t> plain_y, soft_y, hard_y, soft_y_b;
    run_with(-1.0f, plain_y, comp_a, bg.uv);            // no shadow at all
    run_with(12.0f, soft_y, comp_a, bg.uv);             // canonical preset blur
    run_with(0.0f, hard_y, comp_a, bg.uv);              // hard offset silhouette
    run_with(12.0f, soft_y_b, comp_b, bg.uv);           // second instance

    // Inside the silhouette the fill still wins: the shadow cannot leak over
    // the glyph body.
    CHECK(luma_at(soft_y, w, center_x, center_y) ==
          luma_at(plain_y, w, center_x, center_y));

    // 7 px OUTSIDE the silhouette, below it: the authored blur must spread the
    // shadow past the glyph edge (and past the hard offset silhouette), while a
    // blur of 0 keeps that hard silhouette and contributes nothing there.
    const int radius_px = static_cast<int>(disc_radius);
    const int below = center_y + radius_px + 7;
    const int soft_delta = luma_at(plain_y, w, center_x, below) -
                           luma_at(soft_y, w, center_x, below);
    const int hard_delta = luma_at(plain_y, w, center_x, below) -
                           luma_at(hard_y, w, center_x, below);
    // 10 px outside the same edge: still inside the authored radius, but the
    // falloff must already be weaker — a step function would be flat here.
    const int reach = center_y + radius_px + 10;
    const int reach_delta = luma_at(plain_y, w, center_x, reach) -
                            luma_at(soft_y, w, center_x, reach);
    std::printf("[NV12-SHADOW] below edge: 7px soft=%d hard=%d | 10px soft=%d"
                " (Y delta)\n",
                soft_delta, hard_delta, reach_delta);
    CHECK(soft_delta > 5);
    CHECK(hard_delta == 0);
    CHECK(soft_delta > reach_delta);
    CHECK(reach_delta >= 0);

    // The offset is authored downward: nothing may darken ABOVE the glyph.
    const int above = center_y - radius_px - 7;
    CHECK(luma_at(soft_y, w, center_x, above) == luma_at(plain_y, w, center_x, above));

    // Beyond the offset + blur envelope the background is untouched.
    const int far_below = center_y + radius_px + 30;
    CHECK(luma_at(soft_y, w, center_x, far_below) ==
          luma_at(plain_y, w, center_x, far_below));

    // Two independent compositor instances must agree bit-exactly.
    const auto shadow_plane = compare_plane(soft_y.data(), soft_y_b.data(), y_bytes);
    std::printf("[NV12-SHADOW] instance agreement: identical=%d diff=%zu/%zu\n",
                shadow_plane.identical ? 1 : 0, shadow_plane.differing,
                shadow_plane.total);
    CHECK(shadow_plane.identical);
}

TEST_CASE("NV12 direct MTSDF: fill is antialiased and stroke width stays in destination pixels") {
    using namespace chronon3d::nv12_equiv;
    using chronon3d::media::CudaLayerResource;
    using chronon3d::media::CudaLayerResourceKind;
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run the MTSDF stroke contract test");
        return;
    }

    CudaSession session;
    if (!session.ok) {
        MESSAGE("CUDA device/context unavailable; MTSDF stroke contract is NOT RUN");
        return;
    }
    session.set_current();

    constexpr int w = 640;
    constexpr int h = 360;
    constexpr int atlas_size = 64;
    constexpr float range_px = 16.0f;
    constexpr float disc_radius = 20.0f;
    constexpr float dst_x0 = 300.375f;
    constexpr float dst_y0 = 140.375f;
    constexpr float dst_extent = 48.0f;  // 0.75 destination pixels per atlas pixel.
    constexpr float center_x = dst_x0 + (atlas_size - 1) * 0.5f * 0.75f;
    constexpr float center_y = dst_y0 + (atlas_size - 1) * 0.5f * 0.75f;
    static_assert(center_x == 324.0f && center_y == 164.0f);

    const std::size_t y_bytes = static_cast<std::size_t>(w) * h;
    const std::size_t uv_bytes = static_cast<std::size_t>(w) * ((h + 1) / 2);
    const std::vector<std::uint8_t> bg_y(y_bytes, 128u);
    const std::vector<std::uint8_t> bg_uv(uv_bytes, 128u);
    const auto atlas = make_disc_atlas(atlas_size, disc_radius, range_px);

    DevBuffer atlas_d, bg_y_d, bg_uv_d, out_y_d, out_uv_d;
    atlas_d.alloc(atlas.size() * sizeof(float));
    atlas_d.upload_async(session.stream, atlas.data());
    bg_y_d.alloc(y_bytes);
    bg_uv_d.alloc(uv_bytes);
    out_y_d.alloc(y_bytes);
    out_uv_d.alloc(uv_bytes);
    bg_y_d.upload_async(session.stream, bg_y.data());
    bg_uv_d.upload_async(session.stream, bg_uv.data());

    chronon3d::runtime::LayerInstance instance;
    instance.kind = chronon3d::runtime::PrimitiveKind::Text;
    instance.resource_index = 0;
    instance.src_x0 = 0.0f;
    instance.src_y0 = 0.0f;
    instance.src_x1 = 1.0f;
    instance.src_y1 = 1.0f;
    instance.dst_x0 = dst_x0;
    instance.dst_y0 = dst_y0;
    instance.dst_x1 = dst_x0 + dst_extent;
    instance.dst_y1 = dst_y0 + dst_extent;
    instance.opacity = 1.0f;
    chronon3d::runtime::GpuLayerBatch batch;
    batch.instances = {instance};

    CudaLayerResource resource;
    resource.kind = CudaLayerResourceKind::Mtsdf;
    resource.rgba = atlas_d.ptr;
    resource.pitch_bytes = atlas_size * static_cast<int>(sizeof(std::uint32_t));
    resource.width = atlas_size;
    resource.height = atlas_size;
    resource.distance_range_px = range_px;
    resource.color = 0xFFFFFFFFu;
    resource.stroke_color = 0xFF000000u;

    chronon3d::media::video::CudaNv12Compositor compositor(session.context);
    const auto render = [&](float stroke_width, std::vector<std::uint8_t>& out_y) {
        resource.stroke_width = stroke_width;
        const std::vector<CudaLayerResource> resources{resource};
        REQUIRE(compositor.composite_direct_nv12_batch(
            batch, resources, bg_y_d.ptr, w, bg_uv_d.ptr, w, out_y_d.ptr, w,
            out_uv_d.ptr, w, static_cast<std::uint32_t>(w),
            static_cast<std::uint32_t>(h), session.stream));
        check_ok(cuStreamSynchronize(session.stream), "cuStreamSynchronize");
        out_y.resize(y_bytes);
        out_y_d.download(out_y.data());
    };

    std::vector<std::uint8_t> fill_only_y, outlined_y;
    render(0.0f, fill_only_y);
    render(3.5f, outlined_y);

    const auto pixel = [&](const std::vector<std::uint8_t>& plane, int x) {
        return luma_at(plane, w, x, static_cast<int>(center_y));
    };
    const int background = 128;
    // The fill edge is a coverage ramp, not a hard on/off threshold.
    CHECK(pixel(fill_only_y, 338) > pixel(fill_only_y, 339));
    CHECK(pixel(fill_only_y, 339) > pixel(fill_only_y, 340));
    CHECK(pixel(fill_only_y, 340) == background);

    // At scale 0.75, x=341 is 2 px outside the disc edge and gets only the
    // remaining quarter-pixel of the authored 3.5px outline's AA ramp. At
    // x=342 the outline must have ended. Multiplying stroke_width by the
    // atlas scale makes the former fully dark and leaves a false fringe at
    // the latter; the destination-pixel equation satisfies both checks.
    CHECK(pixel(outlined_y, 340) < background - 40);
    CHECK(pixel(outlined_y, 341) > pixel(outlined_y, 340));
    CHECK(pixel(outlined_y, 341) < background - 10);
    CHECK(pixel(outlined_y, 342) == background);

    // A radial probe on the opposite side checks that the width does not
    // depend on the sign of the distance-field gradient.
    CHECK(luma_at(outlined_y, w, static_cast<int>(center_x) - 18,
                  static_cast<int>(center_y)) == background);
}

TEST_CASE("cuda and vulkan mtsdf coverage agree for stroke widths") {
    constexpr int atlas_size = 64;
    constexpr float range_px = 16.0f;
    constexpr float disc_radius = 20.0f;
    const auto atlas = make_disc_atlas(atlas_size, disc_radius, range_px);

    auto tap_alpha = [&](int sx, int sy) -> float {
        sx = std::clamp(sx, 0, atlas_size - 1);
        sy = std::clamp(sy, 0, atlas_size - 1);
        std::uint32_t packed = 0;
        std::memcpy(&packed, &atlas[static_cast<std::size_t>(sy) * atlas_size + sx], sizeof(packed));
        return static_cast<float>((packed >> 24u) & 0xFFu) / 255.0f;
    };

    // 1. CPU replica of CUDA kernel (cuda_nv12_kernels.hpp direct_mtsdf_signed_distance + direct_layer_load)
    auto eval_cuda = [&](float stroke, float scale, float px, float py,
                         float dst_x0, float dst_y0, float dst_extent) -> float {
        const float u = (px - dst_x0) / dst_extent;
        const float v = (py - dst_y0) / dst_extent;
        if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) return 0.0f;

        const float tx = u * static_cast<float>(atlas_size) - 0.5f;
        const float ty = v * static_cast<float>(atlas_size) - 0.5f;
        const int x0 = std::clamp(static_cast<int>(std::floor(tx)), 0, atlas_size - 1);
        const int y0 = std::clamp(static_cast<int>(std::floor(ty)), 0, atlas_size - 1);
        const int x1 = std::min(x0 + 1, atlas_size - 1);
        const int y1 = std::min(y0 + 1, atlas_size - 1);
        const float fx = std::clamp(tx - std::floor(tx), 0.0f, 1.0f);
        const float fy = std::clamp(ty - std::floor(ty), 0.0f, 1.0f);
        const float top = tap_alpha(x0, y0) * (1.0f - fx) + tap_alpha(x1, y0) * fx;
        const float bottom = tap_alpha(x0, y1) * (1.0f - fx) + tap_alpha(x1, y1) * fx;
        const float a_channel = top * (1.0f - fy) + bottom * fy;

        const float source_scale = scale;
        const float field_px = (a_channel - 0.5f) * range_px * source_scale;
        const float fill_alpha = std::clamp(field_px + 0.5f, 0.0f, 1.0f);
        float stroke_alpha = 0.0f;
        if (stroke > 0.0f) {
            stroke_alpha = std::clamp(field_px + 0.5f * stroke + 0.5f, 0.0f, 1.0f);
        }
        return fill_alpha + stroke_alpha * (1.0f - fill_alpha);
    };

    // 2. Vulkan reference decoder (src/backends/vulkan/shaders/text_tile_raster.comp)
    auto eval_vulkan = [&](float stroke, float scale, float px, float py,
                           float dst_x0, float dst_y0, float dst_extent) -> float {
        const float plane_w = dst_extent;
        const float plane_h = dst_extent;
        const float local_x = px - dst_x0;
        const float local_y = py - dst_y0;
        if (local_x < 0.0f || local_x >= plane_w || local_y < 0.0f || local_y >= plane_h) {
            return 0.0f;
        }
        const float texel_to_screen = scale;
        const float local_to_screen = 1.0f;
        const float glyph_uv_x = local_x / plane_w;
        const float glyph_uv_y = local_y / plane_h;
        const float glyph_texel_x = glyph_uv_x * static_cast<float>(atlas_size) - 0.5f;
        const float glyph_texel_y = glyph_uv_y * static_cast<float>(atlas_size) - 0.5f;

        const float max_texel = static_cast<float>(atlas_size - 1);
        const float clamped_tx = std::clamp(glyph_texel_x, 0.0f, max_texel);
        const float clamped_ty = std::clamp(glyph_texel_y, 0.0f, max_texel);
        const int i0_x = static_cast<int>(std::floor(clamped_tx));
        const int i0_y = static_cast<int>(std::floor(clamped_ty));
        const int i1_x = std::min(i0_x + 1, atlas_size - 1);
        const int i1_y = std::min(i0_y + 1, atlas_size - 1);
        const float fx = clamped_tx - static_cast<float>(i0_x);
        const float fy = clamped_ty - static_cast<float>(i0_y);

        const float c00 = tap_alpha(i0_x, i0_y);
        const float c10 = tap_alpha(i1_x, i0_y);
        const float c01 = tap_alpha(i0_x, i1_y);
        const float c11 = tap_alpha(i1_x, i1_y);
        const float msd = (c00 * (1.0f - fx) + c10 * fx) * (1.0f - fy) +
                          (c01 * (1.0f - fx) + c11 * fx) * fy;

        const float fill_alpha = std::clamp((msd - 0.5f) * range_px * texel_to_screen + 0.5f, 0.0f, 1.0f);
        float stroke_alpha = 0.0f;
        if (stroke > 0.0f) {
            stroke_alpha = std::clamp((msd - 0.5f) * range_px * texel_to_screen +
                                      0.5f * stroke * local_to_screen + 0.5f, 0.0f, 1.0f);
        }
        return fill_alpha + stroke_alpha * (1.0f - fill_alpha);
    };

    // 3. Documented tolerance: within 1/255 (single 8-bit quantization step)
    constexpr float kMaxAllowedDiff = 1.0f / 255.0f;

    for (auto stroke : {0.0f, 1.5f, 3.5f, 8.0f}) {
        for (auto scale : {0.25f, 0.31f, 1.0f}) {
            const float dst_extent = static_cast<float>(atlas_size) * scale;
            const float dst_x0 = 100.0f;
            const float dst_y0 = 100.0f;
            const float center = dst_x0 + dst_extent * 0.5f;
            const float disc_radius_dest = disc_radius * scale;

            // Probe on the edge band: from inside the glyph boundary out to stroke halo
            float max_diff = 0.0f;
            const float halo = 0.5f * stroke + range_px * scale + 2.0f;
            const float r_min = std::max(0.0f, disc_radius_dest - halo);
            const float r_max = std::min(dst_extent * 0.5f - 1.0f, disc_radius_dest + halo);

            for (float r = r_min; r <= r_max; r += 0.25f) {
                const float px = center + r;
                const float py = center;
                const float alpha_cuda = eval_cuda(stroke, scale, px, py, dst_x0, dst_y0, dst_extent);
                const float alpha_vk = eval_vulkan(stroke, scale, px, py, dst_x0, dst_y0, dst_extent);
                const float diff = std::abs(alpha_cuda - alpha_vk);
                if (diff > max_diff) {
                    max_diff = diff;
                }
            }
            CHECK(max_diff <= kMaxAllowedDiff);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════
// Determinism: each instance is bit-exact across identical re-runs.
// ═════════════════════════════════════════════════════════════════════════
TEST_CASE("NV12 compositor: instances are bit-exact across identical re-runs") {
    using namespace chronon3d::nv12_equiv;
    if (!gpu_tests_enabled()) {
        MESSAGE("set CHRONON3D_RUN_CUDA_TESTS=1 to run NV12 determinism tests");
        return;
    }

    CudaSession session;
    if (!session.ok) {
        MESSAGE("CUDA device/context unavailable; NV12 determinism test is NOT RUN");
        return;
    }
    session.set_current();

    chronon3d::media::video::CudaNv12Compositor comp_a(session.context);
    chronon3d::media::video::CudaNv12Compositor comp_b(session.context);

    const int w = 640;
    const int h = 360;
    const auto seed = 0xD1CEULL;
    const auto bg = make_background_nv12(w, h, seed);
    const auto rgba = make_rgba_overlay(160, 120, seed, 2);

    const std::size_t y_bytes = static_cast<std::size_t>(w) * h;
    const std::size_t uv_bytes = static_cast<std::size_t>(w) * ((h + 1) / 2);

    // Pre-uploaded overlay resource shared by all four runs.
    DevBuffer overlay_d;
    overlay_d.alloc(rgba.size() * sizeof(float));
    overlay_d.upload_async(session.stream, rgba.data());
    chronon3d::media::CudaLayerResource resource;
    resource.kind = chronon3d::media::CudaLayerResourceKind::Rgba;
    resource.rgba = overlay_d.ptr;
    resource.pitch_bytes = 160 * 16;
    resource.width = 160;
    resource.height = 120;

    chronon3d::runtime::LayerInstance instance;
    instance.kind = chronon3d::runtime::PrimitiveKind::Image;
    instance.resource_index = 0;
    instance.src_x0 = 0.0f; instance.src_y0 = 0.0f;
    instance.src_x1 = 1.0f; instance.src_y1 = 1.0f;
    instance.dst_x0 = 0.0f; instance.dst_y0 = 0.0f;
    instance.dst_x1 = static_cast<float>(w); instance.dst_y1 = static_cast<float>(h);
    instance.opacity = 0.75f;

    chronon3d::runtime::GpuLayerBatch batch;
    batch.instances = {instance};
    const std::vector<chronon3d::media::CudaLayerResource> resources = {resource};

    DevBuffer bg_y_d, bg_uv_d;
    bg_y_d.alloc(y_bytes);
    bg_uv_d.alloc(uv_bytes);

    auto run_once = [&](chronon3d::media::video::CudaNv12Compositor* a,
                        chronon3d::media::video::CudaNv12Compositor* b,
                        std::vector<std::uint8_t>& out_y,
                        std::vector<std::uint8_t>& out_uv,
                        DevBuffer& out_y_d, DevBuffer& out_uv_d) {
        out_y_d.alloc(y_bytes);
        out_uv_d.alloc(uv_bytes);
        bg_y_d.upload_async(session.stream, bg.y.data());
        bg_uv_d.upload_async(session.stream, bg.uv.data());
        if (a) {
            REQUIRE(a->composite_direct_nv12_batch(
                batch, resources, bg_y_d.ptr, w, bg_uv_d.ptr, w,
                out_y_d.ptr, w, out_uv_d.ptr, w,
                static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), session.stream));
        } else {
            REQUIRE(b->composite_direct_nv12_batch(
                batch, resources, bg_y_d.ptr, w, bg_uv_d.ptr, w,
                out_y_d.ptr, w, out_uv_d.ptr, w,
                static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), session.stream));
        }
        check_ok(cuStreamSynchronize(session.stream), "cuStreamSynchronize");
        out_y.resize(y_bytes);
        out_uv.resize(uv_bytes);
        out_y_d.download(out_y.data());
        out_uv_d.download(out_uv.data());
    };

    std::vector<std::uint8_t> a1_y, a1_uv, a2_y, a2_uv, b1_y, b1_uv, b2_y, b2_uv;
    DevBuffer a1_y_d, a1_uv_d, a2_y_d, a2_uv_d, b1_y_d, b1_uv_d, b2_y_d, b2_uv_d;
    run_once(&comp_a, nullptr, a1_y, a1_uv, a1_y_d, a1_uv_d);
    run_once(&comp_a, nullptr, a2_y, a2_uv, a2_y_d, a2_uv_d);
    run_once(nullptr, &comp_b, b1_y, b1_uv, b1_y_d, b1_uv_d);
    run_once(nullptr, &comp_b, b2_y, b2_uv, b2_y_d, b2_uv_d);

    const auto ay = compare_plane(a1_y.data(), a2_y.data(), y_bytes);
    const auto auv = compare_plane(a1_uv.data(), a2_uv.data(), uv_bytes);
    const auto by = compare_plane(b1_y.data(), b2_y.data(), y_bytes);
    const auto buv = compare_plane(b1_uv.data(), b2_uv.data(), uv_bytes);

    std::printf("[NV12-EQ] determinism: A Y identical=%d UV identical=%d | "
                "B Y identical=%d UV identical=%d\n",
                ay.identical ? 1 : 0, auv.identical ? 1 : 0,
                by.identical ? 1 : 0, buv.identical ? 1 : 0);

    CHECK(ay.identical);
    CHECK(auv.identical);
    CHECK(by.identical);
    CHECK(buv.identical);
}

#else // !CHRONON3D_ENABLE_CUDA_INTEROP

TEST_CASE("NV12 contract harness is unavailable without CUDA interop") {
    CHECK(true);
}

#endif
