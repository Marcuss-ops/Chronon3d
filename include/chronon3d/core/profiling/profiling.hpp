#pragma once

#include <chronon3d/core/profiling/trace.hpp>
#include <chronon3d/core/profiling/trace_categories.hpp>

#ifdef CHRONON_PROFILING
#include <tracy/Tracy.hpp>
#define CHRONON_ZONE(name) \
    ZoneScopedN(name); \
    ::chronon3d::TraceScope _tscope(::chronon3d::profiling::g_current_trace, name, "default", ::chronon3d::profiling::g_current_frame)
#define CHRONON_ZONE_C(name, cat) \
    ZoneScopedN(name); \
    ::chronon3d::TraceScope _tscope(::chronon3d::profiling::g_current_trace, name, cat, ::chronon3d::profiling::g_current_frame)
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#ifndef ZoneScopedN
#define ZoneScopedN(name)
#endif
#define CHRONON_ZONE(name) \
    ::chronon3d::TraceScope _tscope(::chronon3d::profiling::g_current_trace, name, "default", ::chronon3d::profiling::g_current_frame)
#define CHRONON_ZONE_C(name, cat) \
    ::chronon3d::TraceScope _tscope(::chronon3d::profiling::g_current_trace, name, cat, ::chronon3d::profiling::g_current_frame)
#endif
