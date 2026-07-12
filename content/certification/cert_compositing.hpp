#pragma once

// ==============================================================================
// content/certification/cert_compositing.hpp
//
// TICKET-COMPOSITING-CERT — Compositing & Effects functional certification
// compositions (P1).
//
// 10 compositions that verify the effects and compositing pipeline:
//
//   CertOpacity         — layer.opacity(0.5) vs opacity(1.0)
//   CertBlur            — layer.blur(8.0) vs no blur
//   CertGlow            — glow + preserve_source contract
//   CertGlowDisabled    — glow with intensity=0 must equal no-glow (no-op)
//   CertShadow          — drop_shadow enabled vs disabled
//   CertStroke          — rect with stroke vs without
//   CertMask            — mask_rect clipping
//   CertBlendAdd        — blend(BlendMode::Add) additive layer
//   CertBlendMultiply   — blend(BlendMode::Multiply) multiplicative layer
//   CertPrecomp         — precomp_layer nested rendering
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

} // namespace chronon3d::content::certification
