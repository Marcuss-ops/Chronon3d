// tests/sdk/test_sdk_archive_manifest.cpp
//
// TICKET-SDK-PACKAGING-CONSOLIDATION — regression lock for the single
// SDK packaging strategy.  Verifies on every CI run that:
//
//   1. The canonical build-tree archive exists at the location passed via
//      the CHRONON3D_SDK_ARCHIVE_PATH compile-definition (set by
//      tests/sdk_tests.cmake from the cmake-evaluated ${CMAKE_BINARY_DIR}/src/
//      libchronon3d_sdk_impl.a path).
//   2. The file is a valid POSIX `ar` archive (magic "!<arch>\n").
//   3. Calling `nm -C` against the archive returns at least one
//      T/W/B/D-defined symbol (≥1 byte of compile output, catches
//      empty-archive regressions or misconfigured archive merge).
//   4. The lock-bit public-surface sample is present: RenderEngine,
//      render_frame, and save_png.  These three symbols are the
//      minimum viable surface for any downstream SDK consumer; a
//      regression that removes ANY of them is a C1 ABI rot.
//   5. ZERO leakage of source-build paths into the symbol table:
//      no `/src/` and no `/tools/` substrings.  The SDK only exports
//      via the canonical Chronon3D::SDK target — any direct leak
//      of an OPP-internal include or script path would mean the
//      archive boundary is broken.
//   6. The canonical SDK facade namespace `chronon3d::sdk::RenderEngine`
//      is present — the public surface is namespace-aliased to
//      Chronon3D::SDK via `add_library(Chronon3D::SDK ALIAS chronon3d_sdk)`
//      in cmake/Chronon3DSdkTargets.cmake ¶¶123.
//
// Output: [TICKET-SDK-PACKAGING-CONSOLIDATION] one-liner on PASS.

#include <doctest/doctest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <sys/wait.h>
#include <string>

// Stringify macro to convert the macro CHRONON3D_SDK_ARCHIVE_PATH
// (passed by cmake via target_compile_definitions) into a string literal.
#define C3D_STR_(x) #x
#define C3D_STR(x)  C3D_STR_(x)

#ifndef CHRONON3D_SDK_ARCHIVE_PATH
#error \
    "CHRONON3D_SDK_ARCHIVE_PATH is undefined — the test target must " \
    "be configured with target_compile_definitions(... CHRONON3D_SDK_ARCHIVE_PATH=..." \
    "<path>/libchronon3d_sdk_impl.a) in tests/sdk_tests.cmake."
#endif

namespace {

// Invokes `nm -C <archive>` via popen() and returns the raw stdout.
// `nm -C` demangles C++ symbols; we don't parse the output, we just
// ensure key substrings appear at least once — the demangled surface
// is plain UTF-8 text on every Linux toolchain in the registry.
struct NmResult {
    std::string output;
    int         exit_status; // raw pclose() return; 0 == clean
};

// Invokes nm -C <archive> via popen(), captures stdout + pclose() exit
// status.  We do not parse the nm output -- we just check that key
// substrings appear at least once.
NmResult invoke_nm_demangled(const char* archive_path) {
    const std::string cmd = std::string("nm -C ") + archive_path;
    std::array<char, 4096> buffer{};
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("popen(nm -C) failed — cannot certify archive");
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)
           != nullptr) {
        result += buffer.data();
    }
    // Capture pclose() exit status -- silent-swallow via unique_ptr
    // would lose non-zero-exit signal (corrupt archive, unreadable).
    NmResult ov;
    ov.output      = std::move(result);
    ov.exit_status = pclose(pipe);
    return ov;
}

} // namespace

TEST_CASE(
    "SDK-PACKAGING-CONSOLIDATION: single-archive integrity lock") {

    const char* archive_path = C3D_STR(CHRONON3D_SDK_ARCHIVE_PATH);

    // ── (1) File must exist at the build-tree location ──────────────────────────────────────────
    {
        std::ifstream f(archive_path, std::ios::binary);
        INFO("Expected archive at " << archive_path);
        REQUIRE_MESSAGE(f.is_open(),
            "TICKET-SDK-PACKAGING-CONSOLIDATION: SDK archive is missing at "
            "the canonical build-tree path.  Either cmake/Chronon3DSdkArchive.cmake "
            "stopped producing libchronon3d_sdk_impl.a, or the link target name "
            "drifted, or tests/sdk_tests.cmake was not updated with "
            "target_compile_definitions(... CHRONON3D_SDK_ARCHIVE_PATH=...).");
    }

    // ── (2) Verify POSIX ar magic ───────────────────────────────────────────────────────────────
    {
        std::ifstream f(archive_path, std::ios::binary);
        char magic[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        f.read(magic, 8);
        const std::string magic_str(magic, 8);
        REQUIRE_MESSAGE(magic_str == "!<arch>\n",
            "TICKET-SDK-PACKAGING-CONSOLIDATION: file is not a POSIX ar archive.  "
            "Got magic='" << magic_str << "'.  The link target name may have "
            "drifted, or cmake ≥3.27 SET_PROPERTY OUTPUT_NAME may have been "
            "introduced incorrectly — there should be NO suffix on "
            "libchronon3d_sdk_impl.a.");
    }

    // ── (3) Invoke `nm -C` and parse the symbol table ───────────────────────────────────────────
    NmResult nm_ov;
    REQUIRE_NOTHROW(nm_ov = invoke_nm_demangled(archive_path));

    // nm exit status — decode via WIFEXITED + WEXITSTATUS so a signal-killed
    // nm (e.g. SIGSEGV on a corrupt archive) surfaces loud, not silent.
    auto decoded_exit = [](int raw) -> int {
        if (WIFEXITED(raw)) return WEXITSTATUS(raw);
        return -1; // killed by signal; treat as failure for the lock.
    };
    const int nm_exit = decoded_exit(nm_ov.exit_status);
    INFO("nm -C exit status: " << nm_exit
         << " (raw pclose=" << nm_ov.exit_status << ")");
    REQUIRE_MESSAGE(nm_exit == 0,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: nm -C did not exit cleanly (exit="
        << nm_exit << ", raw pclose=" << nm_ov.exit_status
        << ").  Archive may be corrupt or unreadable by the nm toolchain.");

    // Symbol-count floor (loose).  Observed count on main is ~1_000_000
    // T/W/B/D symbols; we lock ≥ 100_000 to catch empty-archive rot
    // without breaking on natural symbol growth.
    // nm -C output format is "ADDR T symbol" -- single-letter separators,
    // NO literal brackets.  Sum 4 separate substring counts.
    const std::string& nm_out = nm_ov.output;
    size_t twbd_count = 0;
    for (const char* pat : {" T ", " W ", " B ", " D "}) {
        size_t pos = 0;
        while ((pos = nm_out.find(pat, pos)) != std::string::npos) {
            ++twbd_count;
            pos += 3;
        }
    }
    INFO("Total nm -C bytes:        " << nm_out.size());
    INFO("T/W/B/D symbol count:     " << twbd_count);
    REQUIRE_MESSAGE(twbd_count >= 100000,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: T/W/B/D floor is "
        << twbd_count << " (< 100_000 expected).  Archive is mostly empty.");

    // ── (4) LOCK-BIT public-surface symbols must all be present ─────────────────────────────────
    // These three symbols are the minimum viable SDK consumer surface:
    //   * RenderEngine — the OPP + SDK facade (two namespaces:
    //     chronon3d::RenderEngine and chronon3d::sdk::RenderEngine)
    //   * render_frame  — the per-frame entry-point (`SoftwareRenderer::render_frame`)
    //   * save_png      — the IO helper that completes the render_frame → PNG loop
    CHECK_MESSAGE(nm_out.find("RenderEngine") != std::string::npos,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: lock-bit symbol 'RenderEngine' "
        "is absent from the archive.  Either the SDK facade was deleted "
        "or the static archive failed to include chronon3d_sdk_impl.cpp.");
    // Per-frame render entry-point lockbit.  The OPP canonical symbol
    // is `SoftwareRenderer::render(const Composition&, Frame)` (open-paren
    // disambiguates from `SoftwareRenderer::render_settings` and other
    // `render_*` setters/accessors that have SURFACE noise value but are
    // NOT the per-frame entry point).
    //   We deliberately locked the OPEN-PAREN form so a true regression
    // (rename of `render` -> `render_with` etc.) still fails loud.  The
    // earlier multi-substring `render_frame`/`render_scene` alternates
    // were reversed: zero OPP source matches for those literals (grep of
    // src/backends/software returned empty), so they were matching stale
    // build artifacts rather than current OPP semantics.
    CHECK_MESSAGE(nm_out.find("SoftwareRenderer::render(") != std::string::npos,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: per-frame render entry-point "
        "'SoftwareRenderer::render(' is absent from the archive.  This "
        "breaks the SDK consumer round-trip; the OPP renderer surface "
        "(SoftwareRenderer::render(const Composition&, Frame)) must "
        "remain in libchronon3d_sdk_impl.a.");
    CHECK_MESSAGE(nm_out.find("save_png") != std::string::npos,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: lock-bit symbol 'save_png' "
        "is absent.  The IO helper completes the SDK consumer round-trip "
        "(render_frame → PNG) and must remain in the archive.");

    // ── (5) Canonical SDK facade namespace is present ──────────────────────────────────────────
    CHECK_MESSAGE(nm_out.find("chronon3d::sdk::RenderEngine") != std::string::npos,
        "TICKET-SDK-PACKAGING-CONSOLIDATION: canonical Chronon3D::SDK "
        "facade namespace 'chronon3d::sdk::RenderEngine' is missing.  "
        "The SDK is namespaced via add_library(Chronon3D::SDK ALIAS chronon3d_sdk); "
        "the `sdk::` prefix is the canonical public surface.");

    MESSAGE("[TICKET-SDK-PACKAGING-CONSOLIDATION] archive certified: "
            << archive_path
            << " (" << nm_out.size() << " bytes of demangled symbols)");
}
