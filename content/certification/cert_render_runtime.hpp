#pragma once

// ==============================================================================
// content/certification/cert_render_runtime.hpp
//
// TICKET-RENDER-RUNTIME-CERT — Render runtime & framebuffer certification
// compositions (P0).
//
// CertRectangle:  single layer with a colored rectangle (red, 400×300, centered)
// CertImage:      single layer with a test image (cert_image_test.png, 400×300)
// CertText:       single layer with text ("RUNTIME CERT", Inter Bold 96pt)
// CertComposite:  4 layers combining rect + image + text + background
//                 (verifies layer order: bg → image → rect → text)
//
// 1920×1080 (16:9) canvas for all 4 compositions.
// All 4 produce distinct sha256 hashes (anti-false-green).
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::certification {

chronon3d::Composition cert_rectangle();
chronon3d::Composition cert_image();
chronon3d::Composition cert_text();
chronon3d::Composition cert_composite();

} // namespace chronon3d::content::certification
