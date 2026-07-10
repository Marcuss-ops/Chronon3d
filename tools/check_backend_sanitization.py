#!/usr/bin/env python3
import os
import sys
import subprocess

def check_no_legacy_files():
    legacy_files = [
        "src/backends/software/software_node_dispatch.cpp",
        "src/backends/software/software_renderer_draw_fallback.cpp"
    ]
    found = False
    for f in legacy_files:
        if os.path.exists(f):
            print(f"ERROR: Legacy file found: {f}")
            found = True
    return not found

def check_no_legacy_references():
    patterns = [
        "software_internal::draw_node",
    ]
    found_issue = False
    
    # Search in src and include
    search_dirs = ["src/backends/software", "include/chronon3d/backends/software"]
    
    for d in search_dirs:
        if not os.path.exists(d): continue
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith((".cpp", ".hpp")):
                    path = os.path.join(root, file)
                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                        for i, line in enumerate(lines):
                            for p in patterns:
                                if p in line and "SoftwareNodeDispatcher::draw_node" not in line:
                                    print(f"ERROR: Legacy pattern '{p}' found in {path}:{i+1}")
                                    found_issue = True
    return not found_issue

def check_no_debug_smoke_signals():
    # Only check in software backend
    patterns = ["std::abort", "printf(", "std::cout", "std::cerr"]
    found_issue = False

    search_dirs = ["src/backends/software", "include/chronon3d/backends/software"]

    # We allow cerr for the SoftwareNodeDispatcher error logging we just added
    allowed_exceptions = {
        "src/backends/software/software_node_dispatcher.cpp": ["std::cerr"]
    }

    for d in search_dirs:
        if not os.path.exists(d): continue
        for root, _, files in os.walk(d):
            for file in files:
                if file.endswith((".cpp", ".hpp")):
                    path = os.path.normpath(os.path.join(root, file))
                    rel_path = os.path.relpath(path, ".").replace("\\", "/")

                    with open(path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                        for i, line in enumerate(lines):
                            for p in patterns:
                                if p in line:
                                    # Check if this is an allowed exception
                                    is_allowed = False
                                    if rel_path in allowed_exceptions:
                                        if any(exc in p for exc in allowed_exceptions[rel_path]):
                                            is_allowed = True

                                    if not is_allowed:
                                        print(f"ERROR: Debug smoke signal '{p}' found in {path}:{i+1}")
                                        found_issue = True
    return not found_issue


def check_tests_have_no_smoke_signals():
    """Tests may legitimately use printf/std::cout for test output, but raw
    std::abort / uncaught exceptions that kill the test runner should not
    appear.  Allow printf/std::cout in test files but flag std::abort and
    explicit std::terminate calls as code smells (they should use
    REQUIRE/CHECK assertions from doctest instead)."""
    found_issue = False
    test_root = "tests"
    if not os.path.exists(test_root):
        return True

    # In test files we allow printf/cout/cerr (they're used for diagnostic
    # output of test results).  We only flag std::abort / std::terminate
    # which kill the test runner without giving doctest a chance to report.
    bad_patterns = ["std::abort(", "std::terminate("]

    for root, _, files in os.walk(test_root):
        for file in files:
            if not file.endswith((".cpp", ".hpp", ".c", ".h")):
                continue
            path = os.path.normpath(os.path.join(root, file))
            rel_path = os.path.relpath(path, ".").replace("\\", "/")
            with open(path, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
                for i, line in enumerate(lines):
                    for p in bad_patterns:
                        if p in line:
                            print(
                                f"ERROR: '{p}' found in test file {path}:{i+1} "
                                f"-- use doctest REQUIRE/CHECK assertions instead"
                            )
                            found_issue = True
    return not found_issue

if __name__ == "__main__":
    ok = True
    print("--- Running Sanitization Checks ---")
    if not check_no_legacy_files(): ok = False
    if not check_no_legacy_references(): ok = False
    if not check_no_debug_smoke_signals(): ok = False
    if not check_tests_have_no_smoke_signals(): ok = False

    if ok:
        print("SUCCESS: All sanitization checks passed!")
        sys.exit(0)
    else:
        print("FAILURE: Sanitization checks failed.")
        sys.exit(1)
