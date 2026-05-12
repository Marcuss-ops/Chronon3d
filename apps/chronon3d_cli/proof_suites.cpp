#include "proof_suites.hpp"
#include <algorithm>

namespace chronon3d {
namespace cli {

const std::vector<ProofSuite>& proof_suites() {
    static const std::vector<ProofSuite> suites = {
        {
            .name = "text",
            .frames = {
                {"TextProof",           0, "text_proof.png"},
                {"TextSizesProof",      0, "text_sizes.png"},
                {"TextAlphaProof",      0, "text_alpha.png"},
                {"TextClippingProof",   0, "text_clipping.png"},
                {"TextDrawOrderProof",  0, "text_draw_order.png"},
                {"TextAlignProof",      0, "text_align.png"},
            }
        },
        {
            .name = "layer",
            .frames = {
                {"LayerProof",                    0,  "layer_proof.png"},
                {"AnimatedLayerProof",            0,  "animated_layer_0000.png"},
                {"AnimatedLayerProof",            30, "animated_layer_0030.png"},
                {"LayerOpacityProof",             0,  "layer_opacity.png"},
                {"LayerDrawOrderProof",           0,  "layer_draw_order.png"},
                {"LayerChildDrawOrderProof",      0,  "layer_child_draw_order.png"},
                {"LayerLocalCoordinatesProof",    0,  "layer_local_coords.png"},
                {"LayerVisibilityProof",          0,  "layer_visibility.png"},
                {"LayerTransformStressProof",     0,  "layer_transform_stress.png"},
            }
        },
        {
            .name = "image",
            .frames = {
                {"ImageProof",                  0,  "image_proof.png"},
                {"AnimatedImageProof",          0,  "animated_image_0000.png"},
                {"AnimatedImageProof",          30, "animated_image_0030.png"},
                {"AnimatedImageProof",          60, "animated_image_0060.png"},
                {"ImageAlphaProof",             0,  "image_alpha.png"},
                {"ImageClippingProof",          0,  "image_clipping.png"},
                {"ImageLayerTransformProof",    0,  "image_layer_transform.png"},
                {"ImageDrawOrderProof",         0,  "image_draw_order.png"},
            }
        },
        {
            .name = "effects",
            .frames = {
                {"ShadowProof",              0,  "shadow.png"},
                {"GlowProof",               0,  "glow.png"},
                {"BlurProof",               0,  "blur.png"},
                {"TintProof",               0,  "tint.png"},
                {"BrightnessContrastProof", 0,  "brightness_contrast.png"},
                {"BlendModeProof",          0,  "blend_modes.png"},
                {"AnimatedBlurRevealProof", 0,  "blur_reveal_0000.png"},
                {"AnimatedBlurRevealProof", 30, "blur_reveal_0030.png"},
                {"AnimatedBlurRevealProof", 60, "blur_reveal_0060.png"},
            }
        },
        {
            .name = "camera25d",
            .frames = {
                {"Camera25DDepthScaleProof",    0,  "camera25d_depth_scale.png"},
                {"Camera25DParallaxProof",      0,  "camera25d_parallax_0000.png"},
                {"Camera25DParallaxProof",      30, "camera25d_parallax_0030.png"},
                {"Camera25DParallaxProof",      60, "camera25d_parallax_0060.png"},
                {"Camera25DZOrderProof",        0,  "camera25d_z_order.png"},
                {"Camera25DCameraPushProof",    0,  "camera25d_push_0000.png"},
                {"Camera25DCameraPushProof",    30, "camera25d_push_0030.png"},
                {"Camera25DCameraPushProof",    60, "camera25d_push_0060.png"},
                {"Camera25DMixed2D3DProof",     0,  "camera25d_mixed_0000.png"},
                {"Camera25DMixed2D3DProof",     59, "camera25d_mixed_0059.png"},
                {"Camera25DImageParallaxProof", 0,  "camera25d_image_parallax_0000.png"},
                {"Camera25DImageParallaxProof", 30, "camera25d_image_parallax_0030.png"},
                {"Camera25DImageParallaxProof", 60, "camera25d_image_parallax_0060.png"},
            }
        },
        {
            .name = "depth",
            .frames = {
                {"DepthRolesProof",  0,  "depth_roles_0000.png"},
                {"DepthRolesProof",  45, "depth_roles_0045.png"},
                {"DepthRolesProof",  90, "depth_roles_0090.png"},
                {"ZSortingProof",    0,  "z_sorting_0000.png"},
                {"DepthOffsetProof",     0,  "depth_offset_0000.png"},
                {"ParallaxRolesProof",   0,  "parallax_0000.png"},
                {"ParallaxRolesProof",   30, "parallax_0030.png"},
                {"ParallaxRolesProof",   60, "parallax_0060.png"},
                {"ParallaxRolesProof",   90, "parallax_0090.png"},
            }
        },
        {
            .name = "camera",
            .frames = {
                {"CameraBasicTwoNodeProof",    0,  "basic_two_node_0000.png"},
                {"CameraFallbackProof",        0,  "fallback_0000.png"},
                {"CameraZoomComparisonProof",  0,  "zoom_0300.png"},
                {"CameraZoomComparisonProof",  1,  "zoom_0877.png"},
                {"CameraZoomComparisonProof",  2,  "zoom_1500.png"},
                {"CameraParallaxProof",        0,  "parallax_0000.png"},
                {"CameraParallaxProof",        30, "parallax_0030.png"},
                {"CameraParallaxProof",        60, "parallax_0060.png"},
                {"CameraParallaxProof",        89, "parallax_0089.png"},
                {"CameraAxisDebugProof",       0,  "axis_debug_0000.png"},
                {"TopViewDebugProof",          0,  "top_view_debug.png"},
            }
        },
        {
            .name = "animation",
            .frames = {
                {"KeyframesProof", 0,   "keyframes_0000.png"},
                {"KeyframesProof", 30,  "keyframes_0030.png"},
                {"KeyframesProof", 80,  "keyframes_0080.png"},
                {"KeyframesProof", 110, "keyframes_0110.png"},
            }
        },
        {
            .name = "masks",
            .frames = {
                {"MaskRectProof",           0,  "mask_rect.png"},
                {"MaskRoundedRectProof",    0,  "mask_rounded_rect.png"},
                {"MaskCircleProof",         0,  "mask_circle.png"},
                {"MaskTextRevealProof",     0,  "mask_text_reveal.png"},
                {"AnimatedMaskRevealProof", 0,  "mask_reveal_0000.png"},
                {"AnimatedMaskRevealProof", 30, "mask_reveal_0030.png"},
                {"AnimatedMaskRevealProof", 60, "mask_reveal_0060.png"},
                {"MaskLayerTransformProof", 0,  "mask_layer_transform.png"},
                {"MaskCamera25DProof",      0,  "mask_camera25d_0000.png"},
                {"MaskCamera25DProof",      30, "mask_camera25d_0030.png"},
                {"MaskCamera25DProof",      60, "mask_camera25d_0060.png"},
            }
        },
    };
    return suites;
}

std::optional<ProofSuite> find_proof_suite(std::string_view name) {
    const auto& all = proof_suites();
    auto it = std::find_if(all.begin(), all.end(),
        [name](const ProofSuite& s) { return s.name == name; });
    if (it == all.end()) return std::nullopt;
    return *it;
}

} // namespace cli
} // namespace chronon3d
