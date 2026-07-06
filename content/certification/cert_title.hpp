#pragma once

// ==============================================================================
// content/certification/cert_title.hpp
//
// FASE 3.1-3.4 — CertTitle / CertLowerThird / CertLongText certification
//
// CertTitle:          1920×1080 (16:9), "EPIC TITLE" Inter Bold 120pt centrato
// CertTitleVertical:  1080×1920 (9:16), stesso testo per TikTok/Shorts
// CertLowerThird:     lower third con box semi-trasparente e safe margins
// CertLongText:       wrapping automatico con line spacing
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::certification {

chronon3d::Composition cert_title();
chronon3d::Composition cert_title_vertical();
chronon3d::Composition cert_lower_third();
chronon3d::Composition cert_long_text();
chronon3d::Composition cert_multilingual();

} // namespace chronon3d::content::certification
