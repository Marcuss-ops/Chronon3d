// ═══════════════════════════════════════════════════════════════════════════
// src/backends/software/software_session_resources.cpp — M1.5#7
// ═══════════════════════════════════════════════════════════════════════════
//
// OUT-OF-LINE definitions for `SoftwareSessionResources`.  Lives in a
// .cpp because the default constructor now needs the complete
// `TextRenderResources` type to `std::make_unique` for the
// `text_resources` member — pulling `<blend2d.h>` into the public
// header would exceed the WP-3 dependency-direction invariant
// (`backends/software/` must NOT expose backend-specific includes to
// header consumers that include this struct as a value type).

#include <chronon3d/backends/software/software_session_resources.hpp>

#include <chronon3d/backends/text/text_render_resources.hpp>

namespace chronon3d {

SoftwareSessionResources::SoftwareSessionResources()
    : text_resources(std::make_unique<TextRenderResources>())
{}

// M1.5#7: unique_ptr auto-deletes the TextRenderResources on
// destruction; explicit cleanup body not required.  Defaulted to keep
// the OOL declaration already in the public header.
SoftwareSessionResources::~SoftwareSessionResources() = default;

} // namespace chronon3d
