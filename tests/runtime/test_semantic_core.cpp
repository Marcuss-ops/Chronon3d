#include <doctest/doctest.h>

#include <chronon3d/cache/video_frame_cache.hpp>
#include <chronon3d/media/frame_conversion/frame_converter.hpp>
#include <chronon3d/media/video/video_frame.hpp>
#include <chronon3d/runtime/frame_format.hpp>
#include <chronon3d/runtime/render_surface.hpp>
#include <chronon3d/runtime/resource_plan.hpp>
#include <chronon3d/runtime/resource_residency.hpp>

#include <type_traits>

namespace rt = chronon3d::runtime;

static_assert(std::is_same_v<rt::ColorMetadata, rt::FrameFormat>);
static_assert(std::is_same_v<chronon3d::video::EncoderPixelFormat, rt::PixelFormat>);
static_assert(std::is_same_v<chronon3d::video::YuvMatrix, rt::ColorMatrix>);
static_assert(std::is_same_v<chronon3d::video::ColorRange, rt::ColorRange>);
static_assert(std::is_same_v<chronon3d::media::video::PixelFormat, rt::PixelFormat>);

TEST_CASE("Phase1 semantic core: canonical FrameFormat owns the complete image semantics") {
    const auto format = rt::canonical_render_format();

    CHECK(format.pixel == rt::PixelFormat::Rgba16Float);
    CHECK(format.primaries == rt::ColorPrimaries::Bt709);
    CHECK(format.transfer == rt::TransferFunction::Linear);
    CHECK(format.matrix == rt::ColorMatrix::Identity);
    CHECK(format.range == rt::ColorRange::Full);
    CHECK(format.alpha == rt::AlphaMode::Premultiplied);
    CHECK(format.pixel_aspect == rt::PixelAspectRatio{1, 1});
    CHECK(format.pixel_aspect.valid());
    CHECK(rt::is_canonical_render_format(format));

    auto anamorphic = format;
    anamorphic.pixel_aspect = rt::PixelAspectRatio{4, 3};
    CHECK_FALSE(rt::is_canonical_render_format(anamorphic));

    auto invalid = format;
    invalid.pixel_aspect.denominator = 0;
    CHECK_FALSE(invalid.valid());
}

TEST_CASE("Phase1 semantic core: GPU RGB surfaces are normalized to one render domain") {
    auto incoming = rt::SurfaceDesc::make(
        320, 180,
        rt::FrameFormat{
            rt::PixelFormat::Rgba8Unorm,
            rt::ColorPrimaries::Bt709,
            rt::TransferFunction::Srgb,
            rt::ColorMatrix::Identity,
            rt::ColorRange::Full,
            rt::ChromaLocation::Left,
            rt::AlphaMode::Straight});

    rt::GpuRgbSurface surface(1, incoming);
    REQUIRE(surface.valid());
    CHECK(surface.desc().format == rt::canonical_render_format());
    CHECK(surface.desc().bytes ==
          rt::tight_surface_bytes(rt::canonical_render_format(), 320, 180));
}

TEST_CASE("Phase1 semantic core: ResourceDesc propagates complete format into physical plan") {
    rt::ResourcePlanner planner;
    rt::ResourceRequest request{};
    request.id = "render-color";
    request.kind = rt::ResourceKind::Color;
    request.first = 0;
    request.last = 0;
    request.desc = rt::ResourceDesc::make(
        64, 64, rt::FrameFormat{rt::PixelFormat::Rgba8Unorm},
        rt::ResourceUsage::ColorAttachment);

    planner.add(request);
    const auto plan = planner.build();

    REQUIRE(plan.slots.size() == 1);
    CHECK(plan.slots[0].format == rt::canonical_render_format());
    CHECK(plan.slots[0].bytes ==
          rt::tight_surface_bytes(rt::canonical_render_format(), 64, 64));
}

TEST_CASE("Phase1 semantic core: aliasing compares full FrameFormat, not only pixel storage") {
    const auto base = rt::FrameFormat{
        rt::PixelFormat::Rgba8Unorm,
        rt::ColorPrimaries::Bt709,
        rt::TransferFunction::Linear,
        rt::ColorMatrix::Identity,
        rt::ColorRange::Full,
        rt::ChromaLocation::Left,
        rt::AlphaMode::Premultiplied,
        rt::PixelAspectRatio{1, 1}};

    SUBCASE("different transfer functions cannot alias") {
        rt::ResourcePlanner planner;

        rt::ResourceRequest a{};
        a.id = "a";
        a.kind = rt::ResourceKind::Bytes;
        a.first = 0;
        a.last = 0;
        a.desc = rt::ResourceDesc::make(32, 32, base);

        auto srgb = base;
        srgb.transfer = rt::TransferFunction::Srgb;
        rt::ResourceRequest b{};
        b.id = "b";
        b.kind = rt::ResourceKind::Bytes;
        b.first = 1;
        b.last = 1;
        b.desc = rt::ResourceDesc::make(32, 32, srgb);

        planner.add(a);
        planner.add(b);
        CHECK(planner.build().slots.size() == 2);
    }

    SUBCASE("different alpha modes cannot alias") {
        rt::ResourcePlanner planner;
        rt::ResourceRequest a{};
        a.id = "a";
        a.kind = rt::ResourceKind::Bytes;
        a.first = 0;
        a.last = 0;
        a.desc = rt::ResourceDesc::make(32, 32, base);

        auto straight = base;
        straight.alpha = rt::AlphaMode::Straight;
        rt::ResourceRequest b{};
        b.id = "b";
        b.kind = rt::ResourceKind::Bytes;
        b.first = 1;
        b.last = 1;
        b.desc = rt::ResourceDesc::make(32, 32, straight);

        planner.add(a);
        planner.add(b);
        CHECK(planner.build().slots.size() == 2);
    }

    SUBCASE("different pixel aspect ratios cannot alias") {
        rt::ResourcePlanner planner;
        rt::ResourceRequest a{};
        a.id = "a";
        a.kind = rt::ResourceKind::Bytes;
        a.first = 0;
        a.last = 0;
        a.desc = rt::ResourceDesc::make(32, 32, base);

        auto anamorphic = base;
        anamorphic.pixel_aspect = rt::PixelAspectRatio{4, 3};
        rt::ResourceRequest b{};
        b.id = "b";
        b.kind = rt::ResourceKind::Bytes;
        b.first = 1;
        b.last = 1;
        b.desc = rt::ResourceDesc::make(32, 32, anamorphic);

        planner.add(a);
        planner.add(b);
        CHECK(planner.build().slots.size() == 2);
    }

    SUBCASE("identical complete semantics can alias when lifetimes do not overlap") {
        rt::ResourcePlanner planner;
        rt::ResourceRequest a{};
        a.id = "a";
        a.kind = rt::ResourceKind::Bytes;
        a.first = 0;
        a.last = 0;
        a.desc = rt::ResourceDesc::make(32, 32, base);

        rt::ResourceRequest b{};
        b.id = "b";
        b.kind = rt::ResourceKind::Bytes;
        b.first = 1;
        b.last = 1;
        b.desc = rt::ResourceDesc::make(32, 32, base);

        planner.add(a);
        planner.add(b);
        CHECK(planner.build().slots.size() == 1);
    }
}

TEST_CASE("Phase1 semantic core: allocation size is derived centrally from ResourceDesc") {
    rt::ResourcePlanner planner;
    rt::ResourceRequest request{};
    request.id = "derived-bytes";
    request.kind = rt::ResourceKind::Bytes;
    request.first = 0;
    request.last = 0;
    request.desc = rt::ResourceDesc::make(
        16, 8, rt::FrameFormat{rt::PixelFormat::Rgba8Unorm});

    // A stale compatibility observation must not become allocation authority.
    request.desc.bytes = 1;
    planner.add(request);
    const auto plan = planner.build();

    REQUIRE(plan.slots.size() == 1);
    CHECK(plan.slots[0].bytes == 16u * 8u * 4u);
}

TEST_CASE("Phase1 semantic core: exportable residency is dedicated and cannot alias") {
    rt::ResourceResidency residency{};
    residency.domain = rt::MemoryDomain::Vulkan;
    residency.device = 2;
    residency.exportable = true;
    residency.encoder_compatible = true;

    rt::ResourcePlanner planner;
    for (int i = 0; i < 2; ++i) {
        rt::ResourceRequest request{};
        request.id = i == 0 ? "a" : "b";
        request.kind = rt::ResourceKind::Bytes;
        request.first = static_cast<std::size_t>(i);
        request.last = static_cast<std::size_t>(i);
        request.desc = rt::ResourceDesc::make(
            64, 64, rt::FrameFormat{rt::PixelFormat::Nv12},
            rt::ResourceUsage::Storage,
            rt::LifetimeClass::FrameTransient,
            alignof(std::max_align_t), residency);
        planner.add(request);
    }

    const auto plan = planner.build();
    REQUIRE(plan.slots.size() == 2);
    CHECK(plan.slots[0].dedicated);
    CHECK(plan.slots[1].dedicated);
    CHECK(plan.slots[0].residency == residency);
    CHECK(plan.slots[1].residency == residency);
}

TEST_CASE("Phase1 semantic core: video cache identity includes pixel aspect ratio") {
    chronon3d::cache::VideoFrameKey square{};
    square.composition_id = "comp";
    square.width = 1920;
    square.height = 1080;
    square.format = rt::make_frame_format(rt::PixelFormat::Yuv420P);

    auto anamorphic = square;
    anamorphic.format.pixel_aspect = rt::PixelAspectRatio{4, 3};

    CHECK(square.digest() != anamorphic.digest());
}
