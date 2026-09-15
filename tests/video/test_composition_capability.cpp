#define DOCTEST_CONFIG_SUPER_FAST_ASSERTS
#include <doctest/doctest.h>

#include <chronon3d/media/video/composition_capability.hpp>

using chronon3d::BlendMode;
using chronon3d::media::video::DirectYuvEffectClass;
using chronon3d::media::video::DirectYuvPolicy;
using chronon3d::media::video::DirectYuvSupport;
using chronon3d::media::video::LayerYuvQuery;
using chronon3d::media::video::classify_direct_yuv;
using chronon3d::media::video::is_certified_direct_yuv;
using chronon3d::media::video::resolve_direct_yuv_support;

namespace {

LayerYuvQuery certified_image_overlay() noexcept {
    LayerYuvQuery q;
    q.effect = DirectYuvEffectClass::ImageOverlay;
    q.gpu_resident = true;
    return q;
}

} // namespace

TEST_CASE("composition capability: certified effect set classifies in-domain") {
    // GPU-resident image overlays, canonical prepared MTSDF text, video
    // background and simple color matrices are Certified on the 2D
    // single-node Normal path. Secondary video overlays remain explicit
    // FullGraph fallbacks until their native NV12 coverage is shipped.
    CHECK(classify_direct_yuv(certified_image_overlay()) ==
          DirectYuvSupport::Certified);

    LayerYuvQuery text;
    text.effect = DirectYuvEffectClass::TextOverlay;
    text.gpu_resident = true;
    CHECK(classify_direct_yuv(text) == DirectYuvSupport::RequiresFullFrame);
    // The prepared MTSDF contract alone is not enough: the run's remaining
    // style semantics must lower faithfully too. The matrix owns that decision
    // through the classifier's flag, so an unrepresentable shadow stack can
    // never be approximated on the zero-conversion lane.
    text.prepared_mtsdf = true;
    CHECK(classify_direct_yuv(text) == DirectYuvSupport::Unsupported);
    CHECK(resolve_direct_yuv_support(text, DirectYuvPolicy::Require).reason ==
          "text_style_unsupported");
    text.text_style_representable = true;
    CHECK(classify_direct_yuv(text) == DirectYuvSupport::Certified);

    LayerYuvQuery generic_gpu_text = text;
    generic_gpu_text.prepared_mtsdf = false;
    CHECK(classify_direct_yuv(generic_gpu_text) ==
          DirectYuvSupport::RequiresFullFrame);

    LayerYuvQuery video_overlay;
    video_overlay.effect = DirectYuvEffectClass::VideoOverlayNv12;
    CHECK(classify_direct_yuv(video_overlay) == DirectYuvSupport::RequiresFullFrame);

    LayerYuvQuery background;
    background.effect = DirectYuvEffectClass::VideoBackground;
    CHECK(classify_direct_yuv(background) == DirectYuvSupport::Certified);

    LayerYuvQuery matrix;
    matrix.effect = DirectYuvEffectClass::ColorMatrixEffect;
    CHECK(classify_direct_yuv(matrix) == DirectYuvSupport::Certified);
}

TEST_CASE("composition capability: Add blend keeps image overlay certified") {
    LayerYuvQuery add = certified_image_overlay();
    add.blend = BlendMode::Add;
    CHECK(is_certified_direct_yuv(add));
}

TEST_CASE("composition capability: non-Normal/Add blend is unsupported") {
    LayerYuvQuery overlay = certified_image_overlay();
    overlay.blend = BlendMode::Screen;
    CHECK(classify_direct_yuv(overlay) == DirectYuvSupport::Unsupported);
    const auto decision =
        resolve_direct_yuv_support(overlay, DirectYuvPolicy::Require);
    CHECK_FALSE(decision.eligible);
    CHECK(decision.blocking);
    CHECK(decision.hard_fail);
    CHECK(decision.reason == "unsupported_blend_mode");
}

TEST_CASE("composition capability: mask/3D/projection/multi-node are unsupported") {
    LayerYuvQuery query = certified_image_overlay();

    query.mask_enabled = true;
    CHECK_FALSE(is_certified_direct_yuv(query));
    CHECK(resolve_direct_yuv_support(query, DirectYuvPolicy::Require).reason ==
          "mask_enabled");

    query = certified_image_overlay();
    query.two_d_transform = false;
    CHECK_FALSE(is_certified_direct_yuv(query));
    CHECK(resolve_direct_yuv_support(query, DirectYuvPolicy::Require).reason ==
          "three_d_transform");

    query = certified_image_overlay();
    query.projection_enabled = true;
    CHECK_FALSE(is_certified_direct_yuv(query));
    CHECK(resolve_direct_yuv_support(query, DirectYuvPolicy::Require).reason ==
          "projection_enabled");

    query = certified_image_overlay();
    query.single_shape_node = false;
    CHECK_FALSE(is_certified_direct_yuv(query));
    CHECK(resolve_direct_yuv_support(query, DirectYuvPolicy::Require).reason ==
          "multi_node_layer");
}

TEST_CASE("composition capability: host-only overlay requires full frame") {
    LayerYuvQuery host_only = certified_image_overlay();
    host_only.gpu_resident = false;
    CHECK(classify_direct_yuv(host_only) == DirectYuvSupport::RequiresFullFrame);

    // Auto policy must fall back explicitly, never hard-fail.
    const auto auto_decision =
        resolve_direct_yuv_support(host_only, DirectYuvPolicy::Auto);
    CHECK_FALSE(auto_decision.eligible);
    CHECK(auto_decision.blocking);
    CHECK_FALSE(auto_decision.hard_fail);

    // Require policy hard-fails.
    const auto require_decision =
        resolve_direct_yuv_support(host_only, DirectYuvPolicy::Require);
    CHECK(require_decision.hard_fail);
}

TEST_CASE("composition capability: shapes and RGB effects need full frame RGBA") {
    LayerYuvQuery shape;
    shape.effect = DirectYuvEffectClass::BasicShapeOverlay;
    CHECK(classify_direct_yuv(shape) == DirectYuvSupport::RequiresFullFrame);

    LayerYuvQuery blur;
    blur.effect = DirectYuvEffectClass::ComplexRgbaEffect;
    CHECK(classify_direct_yuv(blur) == DirectYuvSupport::RequiresFullFrame);

    LayerYuvQuery cpu;
    cpu.effect = DirectYuvEffectClass::CpuEffect;
    CHECK(classify_direct_yuv(cpu) == DirectYuvSupport::CpuOnly);

    LayerYuvQuery unknown;
    unknown.effect = DirectYuvEffectClass::UnsupportedFeature;
    CHECK(classify_direct_yuv(unknown) == DirectYuvSupport::Unsupported);
}

TEST_CASE("composition capability: certified decision under both policies") {
    const LayerYuvQuery query = certified_image_overlay();
    for (const DirectYuvPolicy policy :
         {DirectYuvPolicy::Auto, DirectYuvPolicy::Require}) {
        const auto decision = resolve_direct_yuv_support(query, policy);
        CHECK(decision.eligible);
        CHECK_FALSE(decision.blocking);
        CHECK_FALSE(decision.hard_fail);
        CHECK(decision.reason == "certified");
    }
    CHECK(chronon3d::media::video::direct_yuv_support_name(
              DirectYuvSupport::Certified) == "certified");
}
