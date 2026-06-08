#pragma once

// ── Internal header: parse /proc/[pid]/stat ──────────────────────────────
//
// Extracted from system_metrics.cpp so that unit tests can exercise the
// parsing logic without reading a real /proc/self/stat file.
//
// Format:
//   pid (comm) state ppid pgrp session tty_nr tpgid flags
//   minflt cminflt majflt cmajflt utime stime cutime cstime ...
//
// comm may contain '(' ')' and spaces, so we find the LAST ')' to locate
// the end of the comm field, then count space-separated fields from there.
//
// Fields we want (0-indexed after comm closing paren):
//   7 = minflt, 9 = majflt, 11 = utime, 12 = stime

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace chronon3d::detail {

inline void parse_proc_stat(const char* buf,
                            uint64_t& utime, uint64_t& stime,
                            uint64_t& minor_faults, uint64_t& major_faults) {
    utime = stime = minor_faults = major_faults = 0;

    const char* last_paren = nullptr;
    for (const char* p = buf; *p; ++p) {
        if (*p == ')') last_paren = p;
    }
    if (!last_paren) return;

    const char* p = last_paren + 1;
    while (*p == ' ') ++p;

    auto skip_field = [&]() {
        while (*p && *p != ' ') ++p;
        while (*p == ' ') ++p;
    };

    auto read_uint64 = [&](uint64_t& out) -> bool {
        while (*p == ' ') ++p;
        char* end = nullptr;
        out = std::strtoull(p, &end, 10);
        if (end == p) return false;
        p = end;
        while (*p == ' ') ++p;
        return true;
    };

    // Skip state(0), ppid(1), pgrp(2), session(3), tty_nr(4), tpgid(5), flags(6)
    for (int i = 0; i < 7; ++i) skip_field();
    if (!*p) return;

    if (!read_uint64(minor_faults)) return;  // minflt (field  7)
    skip_field();                             // cminflt (field  8)
    if (!read_uint64(major_faults)) return;  // majflt (field  9)
    skip_field();                             // cmajflt (field 10)
    if (!read_uint64(utime)) return;          // utime   (field 11)
    if (!read_uint64(stime)) return;          // stime   (field 12)
}

} // namespace chronon3d::detail
