// SPDX-License-Identifier: MIT
//
// src/text/compiler/text_compile_validation.cpp — M1.5#5 stage 1.
//
// validate_layout_request: pointer NULL-checks + EmptySource early-out.
// Verbatim move from the historical inline body in
// `src/text/text_run_builder.cpp::compile_text_layout` (pre-M1.5#5
// monolith lines 332-345).  No behavioural mutations.
//
// Stage ordering rationale: this stage runs BEFORE resolve, BEFORE
// tree-walk guards, BEFORE everything else.  Pointer NULLs and empty
// doc are both loud failures that should be reported before any
// resolver or shaping work is initiated.
//
// Use site: orphans: only `text_run_builder.cpp::compile_text_layout`
//          (the orchestrator).  Cross-TU access via internal header
//          `text_compile_internal.hpp` in src/text/compiler/.

#include "text_compile_internal.hpp"

namespace chronon3d::text_internal::compile {

Result<ValidatedRefs, TextLayoutError>
validate_layout_request(
    const TextLayoutRequest& request,
    TextCompileServices& services
) {
    // ── Validate pointers ─────────────────────────────────────────
    if (request.doc == nullptr || request.layout == nullptr) {
        return TextLayoutError{
            TextLayoutErrorKind::MalformedLayout,
            "compile_text_layout: request.doc or request.layout is null"
        };
    }
    // FU02next — pre-render invariant: MissingFontEngine is the
    // canonical Kind for "no FontEngine wired at compile time".
    // Renamed from ShapingFailed (the previous wrong-label) as part
    // of FU02next's invariant taxonomy alignment.  Mirrors the
    // RenderErrorCode::MissingFontEngine (render_diagnostic.hpp)
    // semantic, kept local to text-complier error vocabulary.
    if (services.engine == nullptr) {
        return TextLayoutError{
            TextLayoutErrorKind::MissingFontEngine,
            "compile_text_layout: services.engine is null"
        };
    }

    ValidatedRefs refs{
        *request.doc,
        *request.layout,
        *services.engine
    };

    // ── Resolve document → paragraph tree (early-out on empty) ─────
    if (refs.doc.utf8.empty() && refs.doc.paragraphs.empty()) {
        return TextLayoutError{
            TextLayoutErrorKind::EmptySource,
            "compile_text_layout: empty source text"
        };
    }

    return refs;
}

} // namespace chronon3d::text_internal::compile
