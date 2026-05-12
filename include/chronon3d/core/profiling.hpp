#pragma once

#ifdef CHRONON_PROFILING
#include <tracy/Tracy.hpp>
#else
#ifndef ZoneScoped
#define ZoneScoped
#endif
#ifndef ZoneScopedN
#define ZoneScopedN(name)
#endif
#endif
