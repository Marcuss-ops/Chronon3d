#include <doctest/doctest.h>

#include "../../src/backends/vulkan/pixel_program_pointwise_encoder.hpp"

#include <chronon3d/render_graph/compiler/pixel_program.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using chronon3d::backends::vulkan::PointwiseGpuOp;
using chronon3d::backends::vulkan::encode_pointwise_ops;
using chronon3d::graph::pixel::PixelProgramOp;
using chronon3d::graph::pixel::PixelProgramOpKind;

} // namespace

TEST_SUITE("PixelProgramPointwiseEncoder") {

TEST_CASE("pointwise stack encodes in order and strips the trailing blend") {
    const std::vector<PixelProgramOp> ops{
        PixelProgramOp::brightness(0.25f),
        PixelProgramOp::contrast(1.5f),
        PixelProgramOp::saturation(0.5f),
        PixelProgramOp::exposure(-1.0f),
        PixelProgramOp::tint(1.0f, 0.5f, 0.0f, 0.3f),
        PixelProgramOp::invert(1.0f),
        PixelProgramOp::opacity(0.8f),
        PixelProgramOp::blend(0)};

    std::vector<PointwiseGpuOp> encoded;
    bool trailing_blend = false;
    std::string reason;
    REQUIRE(encode_pointwise_ops(ops, encoded, trailing_blend, reason));
    CHECK(trailing_blend);
    REQUIRE(encoded.size() == 7);
    CHECK(encoded[0].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Brightness));
    CHECK(encoded[1].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Contrast));
    CHECK(encoded[2].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Saturation));
    CHECK(encoded[3].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Exposure));
    CHECK(encoded[4].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Tint));
    CHECK(encoded[5].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Invert));
    CHECK(encoded[6].kind == static_cast<std::uint32_t>(PixelProgramOpKind::Opacity));
}

TEST_CASE("param slots are carried verbatim into the std430 record") {
    const std::vector<PixelProgramOp> ops{PixelProgramOp::tint(0.1f, 0.2f, 0.3f, 0.4f)};
    std::vector<PointwiseGpuOp> encoded;
    bool trailing_blend = true;
    std::string reason;
    REQUIRE(encode_pointwise_ops(ops, encoded, trailing_blend, reason));
    CHECK_FALSE(trailing_blend);
    REQUIRE(encoded.size() == 1);
    CHECK(encoded[0].params[0] == 0.1f);
    CHECK(encoded[0].params[1] == 0.2f);
    CHECK(encoded[0].params[2] == 0.3f);
    CHECK(encoded[0].params[3] == 0.4f);
    CHECK(sizeof(PointwiseGpuOp) == 112);
}

TEST_CASE("unsupported kinds fail closed with a reason") {
    const std::vector<PixelProgramOp> levels{PixelProgramOp(PixelProgramOpKind::Levels)};
    std::vector<PointwiseGpuOp> encoded;
    bool trailing_blend = false;
    std::string reason;
    CHECK_FALSE(encode_pointwise_ops(levels, encoded, trailing_blend, reason));
    CHECK_FALSE(reason.empty());
    CHECK(encoded.empty());

    const std::vector<PixelProgramOp> noise{PixelProgramOp(PixelProgramOpKind::Noise)};
    CHECK_FALSE(encode_pointwise_ops(noise, encoded, trailing_blend, reason));
}

TEST_CASE("a blend that is not the final op is rejected") {
    const std::vector<PixelProgramOp> ops{
        PixelProgramOp::blend(0), PixelProgramOp::opacity(0.5f)};
    std::vector<PointwiseGpuOp> encoded;
    bool trailing_blend = false;
    std::string reason;
    CHECK_FALSE(encode_pointwise_ops(ops, encoded, trailing_blend, reason));
    CHECK_FALSE(reason.empty());
}

} // TEST_SUITE
