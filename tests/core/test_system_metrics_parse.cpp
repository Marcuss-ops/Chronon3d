#include <doctest/doctest.h>
#include "src/core/system_metrics_parse.hpp"

using chronon3d::detail::parse_proc_stat;

TEST_CASE("parse_proc_stat - basic fields") {
    const char* stat = "12345 (test_proc) S 100 200 300 400 500 600 "
                        "777 888 999 1010 1111 1212 1313 1415";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(minflt == 777);
    CHECK(majflt == 999);
    CHECK(utime == 1111);
    CHECK(stime == 1212);
}

TEST_CASE("parse_proc_stat - comm with spaces and parentheses") {
    // The comm field "(my cool process (v2))" contains inner parens + spaces.
    // parse_proc_stat uses the LAST ')' to find the end of comm.
    const char* stat = "42 (my cool process (v2)) S 10 20 30 40 50 60 "
                        "11 22 33 44 55 66 77 88";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(minflt == 11);
    CHECK(majflt == 33);
    CHECK(utime == 55);
    CHECK(stime == 66);
}

TEST_CASE("parse_proc_stat - comm with only one paren (simple name)") {
    const char* stat = "7 (bash) S 1 2 3 4 5 6 "
                        "10 20 30 40 50 60 70 80";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(minflt == 10);
    CHECK(majflt == 30);
    CHECK(utime == 50);
    CHECK(stime == 60);
}

TEST_CASE("parse_proc_stat - zero fields") {
    const char* stat = "1 (init) S 0 0 0 0 0 0 "
                        "0 0 0 0 0 0 0 0";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(minflt == 0);
    CHECK(majflt == 0);
    CHECK(utime == 0);
    CHECK(stime == 0);
}

TEST_CASE("parse_proc_stat - large realistic values") {
    // Simulate a long-running render process
    const char* stat = "999999 (chronon3d_render) R 1000 2000 3000 4000 5000 6000 "
                        "18446744073709551615 "    // minflt = 2^64-1
                        "888 "
                        "18446744073709551615 "    // majflt = 2^64-1
                        "1010 "
                        "18446744073709551615 "    // utime  = 2^64-1
                        "18446744073709551615";     // stime  = 2^64-1
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(minflt == UINT64_MAX);
    CHECK(majflt == UINT64_MAX);
    CHECK(utime == UINT64_MAX);
    CHECK(stime == UINT64_MAX);
}

TEST_CASE("parse_proc_stat - no closing paren") {
    const char* stat = "12345 (no_close_paren";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    // No ')' found → all outputs remain 0
    CHECK(utime == 0);
    CHECK(stime == 0);
    CHECK(minflt == 0);
    CHECK(majflt == 0);
}

TEST_CASE("parse_proc_stat - empty string") {
    const char* stat = "";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    CHECK(utime == 0);
    CHECK(stime == 0);
    CHECK(minflt == 0);
    CHECK(majflt == 0);
}

TEST_CASE("parse_proc_stat - only state field after paren") {
    const char* stat = "12345 (test) S";
    uint64_t utime = 0, stime = 0, minflt = 0, majflt = 0;
    parse_proc_stat(stat, utime, stime, minflt, majflt);

    // Only state (field 0), no more data → all outputs remain 0
    CHECK(utime == 0);
    CHECK(stime == 0);
    CHECK(minflt == 0);
    CHECK(majflt == 0);
}
