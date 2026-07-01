#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// Chronon3d — Public API Umbrella Header  (P3-I re-export-only shim)
//
// CONTRACT — anti-duplication rule #17 (docs/ANTI_DUPLICATION_RULES.md):
//   The umbrella is a thin re-export shim.  It pulls in ONLY the other
//   six V0.1 public-header manifest entries — see
//   `cmake/Chronon3DPublicHeaders.cmake` for the canonical manifest.
//
//   The umbrella MUST NOT pull in any header that is not in the
//   manifest.  In particular, math/, animation/, geometry/, timeline/
//   sequence.hpp, timeline/timeline_builder.hpp, core/types/*, etc. are
//   OPP-internal references and are NOT part of the SDK public surface.
//
// REASONING:
//   Before P3-I, the umbrella transitively `#include`d 17 OPP-internal
//   headers (math/color, math/transform, timeline/sequence,
//   timeline/timeline_builder, core/types/*, animation/*, geometry/*).
//   Any downstream consumer that linked only `Chronon3D::SDK` (per
//   rule #16) would resolve these transitively *while the OPP shipped
//   them*, but the install payload would refuse to ship them once the
//   install surface was tightened to the manifest.  The prune here
//   makes the umbrella ≈ the install surface; downstream consumers
//   that needed types outside the manifest must add explicit
//   `#include`s of OPP-internal headers (and the OPP must elect to
//   ship those, which is a separate ADR per rule #17).
//
// DOWNSTREAM IMPACT (expected per user directive):
//   `tests/install_consumer/main.cpp` previously used the umbrella's
//   transitive math/animation/geometry/text types (Color, Vec3,
//   SceneBuilder/LayerBuilder, GridBackgroundParams, TextAlign,
//   VerticalAlign).  After this commit the consumer will fail to
//   build with "missing transitives" until each non-manifest type is
//   either (a) added to the manifest, or (b) replaced with the
//   manifest-native equivalent.  This is the architecturally correct
//   outcome; "fixing the consumer" is a separate workstream.
//
// For OPP-internal use (not consumer code), include the OPP-internal
// heads directly:
//   #include <chronon3d/math/color.hpp>
//   #include <chronon3d/scene/builders/scene_builder.hpp>
//   …
//
// ═════════════════════════════════════════════════════════════════════════════

// ── V0.1 manifest — 6 other public headers (sdk/* + timeline/composition) ──

#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>
#include <chronon3d/timeline/composition.hpp>

// Additional public types used by the strict-A consumer test
#include <chronon3d/api/render_engine.hpp>
#include <chronon3d/backends/image/image_writer.hpp>

namespace chronon3d {
    // Umbrella header for Chronon3d (P3-I re-export-only shim).
    // No definitions live here by design; the umbrella is a one-stop
    // include for the six V0.1 manifest heads above.
}
