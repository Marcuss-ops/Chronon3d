// tests/install_consumer/main.cpp
//
// ── End-to-end install boundary validation (TICKET-011 final gate) ──
//
// This is a STANDALONE consumer project — it does NOT share
// tests/CMakeLists.txt and does NOT link against the in-tree targets.
// Its only dependency is the *installed* Chronon3D package.
//
// The fact that this file:
//   1. Compiles against `<prefix>/include/chronon3d/...` headers
//   2. Links against `libchronon3d_sdk_impl.a` (transitively via
//      Chronon3D::SDK)
//   3. Runs and emits the [BOUNDARY-OK] marker string
// …proves that the SDK install is consumable by an external project
// downstream.
//
// Wired into CTest via the entry added in the top-level CMakeLists.txt
// (option CHRONON3D_BUILD_INSTALL_CONSUMER_TEST, enabled by the
// linux-ci preset). The orchestrator script
// `tools/install_consumer_test.sh` runs the full configure -> build ->
// install -> consume -> run pipeline against an isolated temp prefix
// and validates the marker.
//
// NOTE: even a minimal main() instantiates `chronon3d::Vec3` and reads
// its members, so the linker is forced to scan libchronon3d_sdk_impl.a
// for the symbol.  Without an actual SDK symbol reference, a static
// archive passes `nm -D` vacuously and the test would not detect a
// corrupt / empty archive.

#include <chronon3d/chronon3d.hpp>

#include <cstdio>

int main() {
    // Force a real symbol lookup against the aggregate archive.
    chronon3d::Vec3 v{1.0f, 2.0f, 3.0f};

    // Boundary marker.  The orchestrator greps the consumer stdout for
    // this exact substring and fails CI if it is absent.
    std::printf("[BOUNDARY-OK] install_consumer linked: "
                "Vec3={%f,%f,%f}\n",
                v.x, v.y, v.z);
    return 0;
}
