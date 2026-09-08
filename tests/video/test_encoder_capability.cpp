#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/encoder_capability.hpp>
#include <chronon3d/media/video/video_execution_resolver.hpp>

using chronon3d::media::CompositePath;
using chronon3d::media::DecodePath;
using chronon3d::media::EncodePath;
using chronon3d::media::EncoderCapabilitySet;
using chronon3d::media::EncoderFeed;
using chronon3d::media::InteropPath;
using chronon3d::media::SurfaceHandoffPath;
using chronon3d::media::VideoExecutionPlan;
using chronon3d::media::resolve_encoder_capabilities;

namespace {

VideoExecutionPlan full_graph_native_plan() noexcept {
    return {DecodePath::Nvdec, CompositePath::VulkanGraph, EncodePath::Nvenc,
            InteropPath::VulkanCuda, SurfaceHandoffPath::Direct};
}

VideoExecutionPlan direct_yuv_plan() noexcept {
    return {DecodePath::Nvdec, CompositePath::DirectYuv, EncodePath::Nvenc,
            InteropPath::CudaNative, SurfaceHandoffPath::Direct};
}

VideoExecutionPlan software_plan() noexcept {
    return {DecodePath::Software, CompositePath::SoftwareGraph,
            EncodePath::Pipe, InteropPath::None, SurfaceHandoffPath::HostUpload};
}

VideoExecutionPlan packet_copy_plan() noexcept {
    return {DecodePath::PacketCopy, CompositePath::None, EncodePath::PacketCopy,
            InteropPath::None, SurfaceHandoffPath::None};
}

} // namespace

TEST_CASE("encoder capability: FullGraph native lane accepts only native surfaces") {
    const EncoderCapabilitySet cap = resolve_encoder_capabilities(full_graph_native_plan());
    CHECK(cap.native_surface);
    CHECK(cap.accepts(EncoderFeed::NativeSurface));
    CHECK_FALSE(cap.accepts(EncoderFeed::HostFrame));
    CHECK_FALSE(cap.accepts(EncoderFeed::DirectYuv));
    CHECK_FALSE(cap.accepts(EncoderFeed::None));
}

TEST_CASE("encoder capability: DirectYuv lane accepts only DirectYuv frames") {
    const EncoderCapabilitySet cap = resolve_encoder_capabilities(direct_yuv_plan());
    CHECK(cap.direct_yuv);
    CHECK(cap.accepts(EncoderFeed::DirectYuv));
    CHECK_FALSE(cap.accepts(EncoderFeed::NativeSurface));
    CHECK_FALSE(cap.accepts(EncoderFeed::HostFrame));
    CHECK_FALSE(cap.accepts(EncoderFeed::None));
}

TEST_CASE("encoder capability: software/pipe lane accepts only host frames") {
    const EncoderCapabilitySet cap = resolve_encoder_capabilities(software_plan());
    CHECK(cap.host_frame);
    CHECK(cap.accepts(EncoderFeed::HostFrame));
    CHECK_FALSE(cap.accepts(EncoderFeed::DirectYuv));
    CHECK_FALSE(cap.accepts(EncoderFeed::NativeSurface));
    CHECK_FALSE(cap.accepts(EncoderFeed::None));
}

TEST_CASE("encoder capability: whole-clip packet copy has no encoder feed") {
    const EncoderCapabilitySet cap = resolve_encoder_capabilities(packet_copy_plan());
    CHECK(cap.none());
    CHECK(cap.accepts(EncoderFeed::None));
    CHECK_FALSE(cap.accepts(EncoderFeed::HostFrame));
    CHECK_FALSE(cap.accepts(EncoderFeed::DirectYuv));
    CHECK_FALSE(cap.accepts(EncoderFeed::NativeSurface));
}

TEST_CASE("encoder capability: GPU encode without composition is still native-surface") {
    // FullGraph GPU encode with software composition absent keeps the
    // native-surface feed: the render output reaches NVENC as a surface.
    const VideoExecutionPlan plan{
        DecodePath::Nvdec, CompositePath::VulkanGraph, EncodePath::Nvenc,
        InteropPath::VulkanCuda, SurfaceHandoffPath::Direct};
    const EncoderCapabilitySet cap = resolve_encoder_capabilities(plan);
    CHECK(cap.accepts(EncoderFeed::NativeSurface));
    CHECK_FALSE(cap.accepts(EncoderFeed::HostFrame));
}

TEST_CASE("encoder capability: feed names are stable and non-empty") {
    CHECK(chronon3d::media::encoder_feed_name(EncoderFeed::None) == "none");
    CHECK(chronon3d::media::encoder_feed_name(EncoderFeed::HostFrame) == "host_frame");
    CHECK(chronon3d::media::encoder_feed_name(EncoderFeed::DirectYuv) == "direct_yuv");
    CHECK(chronon3d::media::encoder_feed_name(EncoderFeed::NativeSurface) == "native_surface");
}
