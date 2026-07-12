// ============================================================================
// tests/sabotage/test_sabotage_main.cpp
// ============================================================================
//
// DOCTEST main implementation for the 9 sabotage tests.
// Per AGENTS.md hygiene gate (tools/check_test_hygiene.sh Check #12):
// only one TU per test executable defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN,
// so the 9 sabotage-test .cpp files are HEADER-ONLY-ASSERTION tests and
// this single file owns the doctest main.
//
// This file is intentionally minimal — it must NOT introduce any
// chronon3d:: symbols, registries, or singletons (Cat-3 anti-duplication
// per AGENTS.md §regole).
//
// Build target: chronon3d_sabotage_tests (raw add_executable,
// EXCLUDE_FROM_ALL — see tests/sabotage_tests.cmake for the rationale).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
