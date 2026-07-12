#pragma once

// ==============================================================================
// content/certification/cert_determinism.hpp
//
// TICKET-DETERMINISM-CERT — Determinism & cache certification composition (P1).
//
// A single deterministic composition used by the canonical
// verify_determinism_linux.sh gate:
//
//   CertDeterminism — solid white rect on dark background (1920×1080).
//                      Every render at the same frame MUST produce
//                      pixel-identical output (same sha256 hash).
//
// 1920×1080 canvas. Determinism guaranteed by fixed params + frame 0.
// ==============================================================================

#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::certification {

chronon3d::Composition cert_determinism();

} // namespace chronon3d::content::certification
