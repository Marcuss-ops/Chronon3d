#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// include/chronon3d/sdk/render_request.hpp
//
// P1-A — Skeleton SDK opaque frame identifier and request type.
// ═════════════════════════════════════════════════════════════════════════════
//
// `sdk::Frame` is the SDK-side ABI-stable frame identifier.  It is
// distinct from the internal `chronon3d::Frame` (<chronon3d/core/
// types/frame.hpp>) so the SDK header set does not pull in any
// `<cstdint>` extras or `fmt` formatters through the OPP-side
// dependency surface.  The SDK impl (PImpl) bridges
// `sdk::Frame ↔ chronon3d::Frame` at the boundary.
//
// `sdk::RenderRequest` is reserved for future batch / accumulated-
// request helpers (tile job submission, multi-frame pipelines);
// keeping the type stable now prevents an ABI break later.  The
// primary entry path remains `RenderEngine::render(const
// Composition&, Frame)`.
// ═════════════════════════════════════════════════════════════════════════════

#include <cstdint>

namespace chronon3d::sdk {

/// SDK-side opaque frame identifier.
struct Frame {
    std::int64_t value{0};
};

/// SDK-side render request.
struct RenderRequest {
    Frame frame{0};                       ///< Frame index, exposed to OPP.
    bool  has_overrides{false};          ///< True ⇒ apply `override_*` fields.
    int   override_width{0};             ///< 0 ⇒ use `RenderSettings::width`.
    int   override_height{0};            ///< 0 ⇒ use `RenderSettings::height`.
    int   override_antialiasing_samples{0}; ///< 0 ⇒ use `RenderSettings::antialiasing_samples`.
};

} // namespace chronon3d::sdk
