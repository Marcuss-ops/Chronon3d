// ============================================================================
// tracing_categories.cpp — Perfetto category registry storage
//
// Exactly ONE translation unit in the program must define the runtime
// storage for the categories declared in tracing_categories.hpp via
// PERFETTO_DEFINE_CATEGORIES().  This is that TU.
//
// The file intentionally does nothing else: the declaration lives in the
// header so every TU that emits CHRONON_TRACE_* events sees the compile-time
// category registry.
// ============================================================================

#include "chronon3d/core/tracing/tracing_categories.hpp"

#ifdef CHRONON3D_ENABLE_TRACING
PERFETTO_TRACK_EVENT_STATIC_STORAGE();
#endif
