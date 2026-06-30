# ============================================================================
# cmake/Chronon3DPublicHeaders.cmake
#
# Single source of truth for the V0.1 SDK public-header manifest.
#
# NO GLOB (`file(GLOB ...)` is the OPP-side anti-pattern: the SDK
# interface is curated by humans, not derived from the on-disk layout).
# Every header that ships inside `libchronon3d_sdk`'s installed payload
# is enumerated here explicitly — adding a header to this list is the
# act of growing the SDK public surface and must be paired with an ADR.
#
# Contract (TICKET-011 cmake-boundary — PUBLIC SURFACE):
#   The OPP-side internals live under `src/*/include_private/` and are
#   NEVER installed.  Only the headers in this list travel with the
#   SDK install payload (delivered to downstream consumers through
#   the `chronon3d_sdk` INTERFACE target's FILE_SET, see
#   cmake/Chronon3DSdkInstall.cmake).
#
# Path style: absolute paths to `${CMAKE_SOURCE_DIR}/include/...`.
# Relative paths inside `cmake/*.cmake` resolve against
# `CMAKE_CURRENT_SOURCE_DIR = <src>/cmake` and would force a fragile
# "`../include`" workaround — absolute is unambiguous and identical
# between the manifest and the consuming `target_sources()` call site.
# ============================================================================

set(CHRONON3D_PUBLIC_HEADERS
    # ── Umbrella ────────────────────────────────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/chronon3d.hpp"

    # ── canonical sdk::* surface (V0.1 MVP) ──────────────────────────────
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_engine.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_output.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_error.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_request.hpp"
    "${CMAKE_SOURCE_DIR}/include/chronon3d/sdk/render_settings.hpp"

    # ── Composition type (canonical timeline leaf, shared by OPP + SDK) ─
    "${CMAKE_SOURCE_DIR}/include/chronon3d/timeline/composition.hpp"
)
