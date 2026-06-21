// =============================================================
// TICKET-011 cmake-boundary OBJECT lift — aggregate marker TU.
//
// `chronon3d_sdk_impl` in src/CMakeLists.txt is declared STATIC.
// Compiled into its own OBJECT library (`chronon3d_sdk_impl_marker`)
// and rolled into the aggregate via `$<TARGET_OBJECTS:...>`.  This
// guarantees that the aggregate's `ar`/archive step is emitted even
// when every per-subsystem OBJECT library is empty (e.g. on a
// per-feature incremental rebuild path).
//
// The `force_archive_emission` symbol is a single volatile byte: it
// keeps the compiled `.o` from being dead-code-eliminated (so the
// archive always has at least one object) while contributing zero
// runtime cost in libraries that consume the aggregate.
//
// This file must NOT be edited by hand.  Its sole purpose is to
// make the aggregate's archive build step explicit so `cmake
// --install` can find `libchronon3d_sdk_impl.a` under
// `<build>/src/`.
// =============================================================

namespace chronon3d::sdk_impl_marker {
    // TICKET-011 cmake-boundary: a single volatile byte ensures
    // the marker TU's `.o` always has at least one symbol that
    // `ar` keeps in the archive, preventing the entire archive
    // from being elided when every per-subsystem OBJECT is empty.
    volatile char force_archive_emission = 0;
}
