// Regression coverage for the native FFmpeg decoder's per-source locking.
// Two stateful AVCodecContext instances must be usable concurrently; a
// decoder-wide lock around seek/decode would serialize this path.

#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/native_video_frame_decoder.hpp>
#include <chronon3d/media/decode_result.hpp>
#include <chronon3d/runtime/cuda_surface_contract.hpp>
#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
#include <chronon3d/media/video/compositor/cuda/cuda_nv12_compositor.hpp>
#endif

#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>

#ifdef _WIN32
#  include <process.h>
#  define CHRONON3D_GETPID _getpid
#else
#  include <unistd.h>
#  define CHRONON3D_GETPID getpid
#endif

namespace {

struct FixtureDirectory {
    FixtureDirectory()
        : path(std::filesystem::temp_directory_path() /
               ("chronon3d-native-decoder-" + std::to_string(CHRONON3D_GETPID()))) {
        std::filesystem::create_directories(path);
    }
    ~FixtureDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    std::filesystem::path path;
};

void write_y4m(const std::filesystem::path& path, std::array<unsigned char, 6> pixels) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\nFRAME\n";
    output.write(reinterpret_cast<const char*>(pixels.data()),
                 static_cast<std::streamsize>(pixels.size()));
    REQUIRE(output.good());
}

void write_two_frame_y4m(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\n";
    output << "FRAME\n";
    const std::array<unsigned char, 6> first{76, 76, 76, 76, 84, 255};
    output.write(reinterpret_cast<const char*>(first.data()),
                 static_cast<std::streamsize>(first.size()));
    output << "FRAME\n";
    const std::array<unsigned char, 6> second{29, 29, 29, 29, 255, 107};
    output.write(reinterpret_cast<const char*>(second.data()),
                 static_cast<std::streamsize>(second.size()));
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("CUDA-interop decode surface is Rgba32Float sized for float4 writes") {
    // The CUDA handoff (CudaNv12Compositor::composite -> nv12_to_rgba)
    // writes RGBA float4 — 16 bytes per pixel — into the imported Vulkan
    // surface via surf2Dwrite, and the render graph samples that surface as
    // an RGBA float storage image.  If the surface were sized from the
    // decoded YUV format (Nv12: 1.5 B/px, P010: 3 B/px) those float4 writes
    // would overrun the external allocation by ~5-10x and fault with
    // CUDA_ERROR_ILLEGAL_ADDRESS at the handoff.  This pins the desc the
    // decoder must produce: Rgba32Float with the allocation covering exactly
    // the float4 view.
    constexpr std::uint32_t width = 1280;
    constexpr std::uint32_t height = 720;
    const auto desc = chronon3d::media::native_decode_surface_desc(width, height);

    CHECK(desc.width == width);
    CHECK(desc.height == height);
    CHECK(desc.format == chronon3d::runtime::PixelFormat::Rgba32Float);
    CHECK(desc.kind == chronon3d::runtime::ResourceKind::Color);
    CHECK(desc.usage == chronon3d::runtime::ResourceUsage::Storage);
    CHECK(desc.lifetime == chronon3d::runtime::LifetimeClass::FrameTransient);
    // float4 write view must fit exactly inside the external allocation.
    CHECK(desc.bytes == static_cast<std::size_t>(width) * height * 16);
    CHECK(desc.bytes ==
          static_cast<std::size_t>(width) * height * sizeof(float) * 4);
    // The regression this guards: a YUV-sized allocation is far smaller and
    // cannot contain the same float4 write view.
    CHECK(desc.bytes > chronon3d::runtime::tight_surface_bytes(
                           chronon3d::runtime::PixelFormat::Nv12, width, height));
    CHECK(desc.bytes > chronon3d::runtime::tight_surface_bytes(
                           chronon3d::runtime::PixelFormat::P010, width, height));
}

TEST_CASE("CUDA external surface contract separates RGBA and DirectYUV lanes") {
    using chronon3d::runtime::CudaSurfaceLane;
    using chronon3d::runtime::PixelFormat;
    using chronon3d::runtime::ResourceKind;
    using chronon3d::runtime::ResourceUsage;
    using chronon3d::runtime::LifetimeClass;

    auto rgba = chronon3d::runtime::SurfaceDesc::make(
        1280, 720, PixelFormat::Rgba32Float, ResourceUsage::Storage,
        LifetimeClass::PipelineSlot);
    rgba.kind = ResourceKind::Color;
    CHECK(chronon3d::runtime::validate_cuda_surface_contract(rgba).lane ==
          CudaSurfaceLane::RgbaCopy);

    auto nv12 = chronon3d::runtime::SurfaceDesc::make(
        1280, 720, PixelFormat::Nv12, ResourceUsage::Storage,
        LifetimeClass::PipelineSlot);
    nv12.kind = ResourceKind::Yuv;
    CHECK(chronon3d::runtime::validate_cuda_surface_contract(nv12).lane ==
          CudaSurfaceLane::Nv12DirectYuv);

    auto p010 = nv12;
    p010.format.pixel = PixelFormat::P010;
    CHECK(chronon3d::runtime::validate_cuda_surface_contract(p010).lane ==
          CudaSurfaceLane::P010DirectYuv);

    auto odd = nv12;
    odd.width = 1279;
    CHECK(!chronon3d::runtime::validate_cuda_surface_contract(odd).valid());

    auto untyped = rgba;
    untyped.kind = ResourceKind::Bytes;
    CHECK(!chronon3d::runtime::validate_cuda_surface_contract(untyped).valid());
}

#ifdef CHRONON3D_ENABLE_CUDA_INTEROP
TEST_CASE("CUDA compositor exposes distinct NV12 and P010 conversion formats") {
    CHECK(static_cast<int>(chronon3d::media::video::CudaYuvFormat::Nv12) !=
          static_cast<int>(chronon3d::media::video::CudaYuvFormat::P010));
}
#endif

TEST_CASE("NativeVideoFrameDecoder decodes independent sources concurrently") {
    FixtureDirectory fixtures;
    const auto source_a = fixtures.path / "a.y4m";
    const auto source_b = fixtures.path / "b.y4m";

    // Red-biased and blue-biased 2x2 YUV420 frames. The contents differ so a
    // future accidental shared decode buffer cannot pass the assertions.
    write_y4m(source_a, {76, 76, 76, 76, 84, 255});
    write_y4m(source_b, {29, 29, 29, 29, 255, 107});

    chronon3d::media::NativeVideoFrameDecoder decoder;
    // This test isolates per-source decoder state; prefetch has its own
    // dedicated concurrency coverage and would add a second decode worker to
    // each source while diagnosing the FFmpeg fixture.
    decoder.set_test_options(chronon3d::media::NativeDecoderTestOptions{
        .enable_prefetch = false,
        .enable_swscale = true,
        .enable_frame_cache = true});
    auto decode = [&decoder](const std::filesystem::path& source) {
        return decoder.decode_frame_at(
            source.string(), chronon3d::RationalTime{0, {1, 30}}, 2, 2);
    };

    auto future_a = std::async(std::launch::async, decode, source_a);
    auto future_b = std::async(std::launch::async, decode, source_b);
    const auto frame_a = future_a.get();
    const auto frame_b = future_b.get();

    const auto* decoded_a = chronon3d::media::decoded_frame_if(frame_a);
    const auto* decoded_b = chronon3d::media::decoded_frame_if(frame_b);
    REQUIRE(decoded_a != nullptr);
    REQUIRE(decoded_b != nullptr);
    REQUIRE(decoded_a->framebuffer != nullptr);
    REQUIRE(decoded_b->framebuffer != nullptr);
    CHECK(decoded_a->framebuffer->width() == 2);
    CHECK(decoded_a->framebuffer->height() == 2);
    CHECK(decoded_b->framebuffer->width() == 2);
    CHECK(decoded_b->framebuffer->height() == 2);
    CHECK(decoded_a->framebuffer->get_pixel(0, 0).r !=
          doctest::Approx(decoded_b->framebuffer->get_pixel(0, 0).r));

    // The original regression was a second software conversion in one
    // session. Keep this explicit same-source sequence beside the concurrent
    // per-source check so the aligned sws staging contract is exercised.
    const auto multi_source = fixtures.path / "multi.y4m";
    write_two_frame_y4m(multi_source);
    chronon3d::media::NativeVideoFrameDecoder sequential_decoder;
    sequential_decoder.set_test_options(chronon3d::media::NativeDecoderTestOptions{
        .enable_prefetch = false,
        .enable_swscale = true,
        .enable_frame_cache = true});
    const auto first = sequential_decoder.decode_frame_at(
        multi_source.string(), chronon3d::RationalTime{0, {1, 30}}, 2, 2);
    const auto second = sequential_decoder.decode_frame_at(
        multi_source.string(), chronon3d::RationalTime{1, {1, 30}}, 2, 2);
    const auto replay = sequential_decoder.decode_frame_at(
        multi_source.string(), chronon3d::RationalTime{0, {1, 30}}, 2, 2);
    const auto* first_decoded = chronon3d::media::decoded_frame_if(first);
    const auto* second_decoded = chronon3d::media::decoded_frame_if(second);
    const auto* replay_decoded = chronon3d::media::decoded_frame_if(replay);
    REQUIRE(first_decoded != nullptr);
    REQUIRE(second_decoded != nullptr);
    REQUIRE(replay_decoded != nullptr);
    REQUIRE(first_decoded->framebuffer != nullptr);
    REQUIRE(second_decoded->framebuffer != nullptr);
    REQUIRE(replay_decoded->framebuffer != nullptr);
    CHECK(first_decoded->framebuffer->width() == 2);
    CHECK(second_decoded->framebuffer->height() == 2);
    CHECK(replay_decoded->framebuffer->height() == 2);
}
