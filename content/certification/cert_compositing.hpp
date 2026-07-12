#pragma once

// ==============================================================================
// content/certification/cert_compositing.hpp
//
// TICKET-COMPOSITING-CERT — Compositing & Effects functional certification
// compositions (P1).
//
// 15 compositions that verify the effects and compositing pipeline
// (per user spec verbatim "testa opacity/blur/glow/stroke/shadow/mask/clip
// + blend normal/additive/multiply/screen + precomp/nested effects"):
//
//   CertOpacity         — layer.opacity(0.5) vs opacity(1.0)
//   CertBlur            — layer.blur(8.0) vs no blur
//   CertGlow            — glow + preserve_source contract (alpha_sum >= plain * 0.95)
//   CertGlowDisabled    — glow with intensity=0 must equal no-glow (sha256 == CertPlain)
//   CertShadow          — drop_shadow enabled vs disabled
//   CertStroke          — rect with stroke vs without
//   CertMask            — mask_circle clipping
//   CertClip            — mask_rect clipping (rect mask = clip path)
//   CertBlendAdd        — blend(BlendMode::Add) additive layer
//   CertBlendNormal     — default blend (no-op): source over destination
//   CertBlendMultiply   — blend(BlendMode::Multiply) multiplicative layer
//   CertBlendScreen     — blend(BlendMode::Screen) brightens: 1 - (1-a)(1-b)
//   CertPrecomp         — precomp_layer nested rendering
//   CertNested          — multiple effects (glow + blur) on one layer
//   CertPlain           — no-effects baseline (for glow no-op sha256 contract)
//
// 1920×1080 canvas. All use Composition + SceneBuilder.
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::certification {

chronon3d::Composition cert_opacity();
chronon3d::Composition cert_blur();
chronon3d::Composition cert_glow();
chronon3d::Composition cert_glow_disabled();
chronon3d::Composition cert_shadow();
chronon3d::Composition cert_stroke();
chronon3d::Composition cert_mask();
chronon3d::Composition cert_blend_add();
chronon3d::Composition cert_blend_multiply();
chronon3d::Composition cert_precomp();
chronon3d::Composition cert_plain();
chronon3d::Composition cert_clip();
chronon3d::Composition cert_blend_normal();
chronon3d::Composition cert_blend_screen();
chronon3d::Composition cert_nested();

} // namespace chronon3d::content::certification
