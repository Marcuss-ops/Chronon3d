#!/bin/bash
set -e

echo "=== Running Architectural & Static Verification Tests ==="
FAILED=0

# Helper to run a test
run_test() {
    local name="$1"
    local command="$2"
    local expected_failure="$3" # 1 if command should find NOTHING, 0 if it should find something
    
    echo -n "Running $name... "
    if [ "$expected_failure" -eq 1 ]; then
        # Expect NO matches (exit code of grep is 1 when no match)
        if eval "$command" > /dev/null 2>&1; then
            echo "FAILED (Unexpected match found!)"
            eval "$command"
            FAILED=1
        else
            echo "PASSED"
        fi
    else
        # Expect matches
        if eval "$command" > /dev/null 2>&1; then
            echo "PASSED"
        else
            echo "FAILED (No match found!)"
            FAILED=1
        fi
    fi
}

# Test 1.1
run_test "Test 1.1: SoftwareRenderer.hpp does not include GraphBuilder" \
  "grep -R \"#include <chronon3d/render_graph/graph_builder.hpp>\" include/chronon3d/backends/software" 1

# Test 1.2
run_test "Test 1.2: SoftwareRenderer.hpp does not include GraphExecutor" \
  "grep -R \"#include <chronon3d/render_graph/graph_executor.hpp>\" include/chronon3d/backends/software" 1

# Test 1.3
run_test "Test 1.3: Software backend does not call GraphBuilder::build" \
  "grep -R \"GraphBuilder::build\" src/backends/software include/chronon3d/backends/software" 1

# Test 1.4
run_test "Test 1.4: Software backend does not create GraphExecutor" \
  "grep -R \"GraphExecutor\" src/backends/software include/chronon3d/backends/software" 1

# Test 1.5
run_test "Test 1.5: software_internal namespace is not in public headers" \
  "grep -R \"namespace software_internal\" include/chronon3d/backends/software" 1

# Test 1.6
# Check that chronon3d_backend_software does not link chronon3d_runtime
# Let's inspect CMakeLists.txt for chronon3d_backend_software target
run_test "Test 1.6: chronon3d_backend_software does not link chronon3d_runtime" \
  "git grep -n \"chronon3d_runtime\" -- CMakeLists.txt | grep -E \"chronon3d_backend_software\"" 1

# Test 2.4: render_scene_internal is only in wrapper and render_pipeline
# Let's find occurrences of render_scene_internal, filtering out comments and test files
run_test "Test 2.4: render_scene_internal is not leaked elsewhere" \
  "grep -Rn \"render_scene_internal\" src include | grep -v \"software_renderer\" | grep -v \"render_pipeline\"" 1

# Test 14.2: SpecScene does not include SoftwareRenderer
run_test "Test 14.2: SpecScene does not include SoftwareRenderer" \
  "grep -Ri \"SoftwareRenderer\" src/specscene include/chronon3d/specscene" 1

# Test 14.3: SpecScene does not include GraphBuilder
run_test "Test 14.3: SpecScene does not include GraphBuilder" \
  "grep -Ri \"GraphBuilder\" src/specscene include/chronon3d/specscene" 1

# Test 14.4: SpecScene does not include GraphExecutor
run_test "Test 14.4: SpecScene does not include GraphExecutor" \
  "grep -Ri \"GraphExecutor\" src/specscene include/chronon3d/specscene" 1

# Test 18.1: Untracked files are not committed
run_test "Test 18.1: No generated/unwanted files tracked" \
  "git ls-files | grep -E '(^build/|^output/|^test_renders/|vcpkg_installed|\.tsbuildinfo|node_modules)'" 1

# Test 18.2: gitignore covers output
run_test "Test 18.2: Gitignore covers output/test.png" \
  "git check-ignore -q output/test.png" 0

run_test "Test 18.2: Gitignore covers build/temp.o" \
  "git check-ignore -q build/temp.o" 0

run_test "Test 18.2: Gitignore covers test_renders/foo.png" \
  "git check-ignore -q test_renders/foo.png" 0

if [ $FAILED -ne 0 ]; then
    echo "=== Static architectural checks FAILED! ==="
    exit 1
else
    echo "=== All static architectural checks PASSED! ==="
    exit 0
fi
