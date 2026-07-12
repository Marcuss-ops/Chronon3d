// ============================================================================
// tests/sabotage/test_sabotage_disk_full.cpp
// ============================================================================
//
// Sabotage scenario #8: video encoder / asset save returned
// ENOSPC (errno 28, "No space left on device") mid-write. The
// pipeline MUST surface an explicit `DiskFull` Result<> error
// rather than silently truncating the output (which corrupts
// downstream playback at the file boundary).
//
// Engine signature (verified-real surfaces): the canonical image
// writer is `ImageWriter` (NOT `save_png()` as a free function —
// fabricated claim REMOVED), declared at
// `include/chronon3d/backends/image/image_writer.hpp`
// (machine-verified via grep 2026-07-12). The encoder-side disk
// write is `NativeEncoder::encode(...)` at `include/chronon3d/
// video/native_encoder.hpp` (also verified-real). The Phase 2
// fail-loud HOOK signature is PLANNED (TICKET-SABOTAGE-PRODUCTION-
// HOOKS): the production hook wires the ENOSPC errno detection
// across BOTH `ImageWriter::write(...)` AND `NativeEncoder::encode
// (...)` paths to emit `DiskFull` Result<> on mid-write failure.
//
// Expected fail-loud path: DiskFull Result<> error + the asset
// save MUST NOT silently leave a truncated file on disk (the
// downstream consumer's read would then fail with a confusing
// "invalid header" error rather than the upstream disk-full
// cause).
//
// Per user spec "Ogni test exit non-zero + identifica comp/layer/node
// + fail-loud":
//   - INFO lines identify comp/layer/node (stub labels).
//   - FAIL_CHECK forces the doctest assertion to fail -> exit 1.
// ============================================================================
#include <doctest/doctest.h>

namespace chaos::sabotage::disk_full {

// Engine-side trigger fingerprint. Implementation forward-point:
// TICKET-SABOTAGE-PRODUCTION-HOOKS — the production hook will
// detect ENOSPC (errno 28) across the verified-real
// `ImageWriter::write(...)` (header at include/chronon3d/backends
// /image/image_writer.hpp) + the verified-real
// `NativeEncoder::encode(...)` (header at include/chronon3d/
// video/native_encoder.hpp) to emit DiskFull.
bool trigger_disk_full_failure() {
    return false;
}

} // namespace chaos::sabotage::disk_full

TEST_CASE(
    "sabotage/disk_full "
    "[comp=ImageWriter, layer=WriterLayer, "
    "node=IMAGE_WRITER_write_ENOSPC_errno_28]"
) {
    INFO("Comp=ImageWriter [verified-real at "
         "include/chronon3d/backends/image/image_writer.hpp]")
    INFO("Layer=WriterLayer [also covers NativeEncoder disk-write "
         "side per native_encoder.hpp]")
    INFO("Node=ImageWriter::write() OR NativeEncoder::encode() "
         "returns ENOSPC (errno 28) [PLANNED "
         "TICKET-SABOTAGE-PRODUCTION-HOOKS]")
    INFO("Sabotage scenario: ImageWriter/NativeEncoder returns "
         "ENOSPC mid-write -> DiskFull")
    INFO("Forward-point: TICKET-SABOTAGE-PRODUCTION-HOOKS")
    FAIL_CHECK(
        "sabotage/disk_full: trigger returns false -- production "
        "fail-loud hook not yet wired "
        "(TICKET-SABOTAGE-PRODUCTION-HOOKS forward-point)"
    );
}
