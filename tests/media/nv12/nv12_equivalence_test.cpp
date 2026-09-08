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

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace chronon3d::media::video::cuda_nv12_detail {
const std::string& get_compiled_nv12_ptx();
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
        if (cuCtxCreate(&context, nullptr, 0, device) != CUDA_SUCCESS) return;
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

TEST_CASE("NV12 PTX cache has stable immutable identity") {
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
    const auto& first = chronon3d::media::video::cuda_nv12_detail::get_compiled_nv12_ptx();
    const auto& second = chronon3d::media::video::cuda_nv12_detail::get_compiled_nv12_ptx();
    REQUIRE_FALSE(first.empty());
    CHECK(&first == &second);
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
