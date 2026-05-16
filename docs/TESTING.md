# Testing Philosophy

The `tests/` directory must remain as minimal as possible.

Tests exist to verify the behavior of the engine, not to recreate engine logic, duplicate runtime flows, or become an alternative implementation of the system.

Every extra line written inside a test is a signal that something may be missing, misplaced, or poorly exposed in the real codebase. If a test needs too much setup, too much scene-building logic, or too much repeated boilerplate, the first assumption should be that the production API, a helper, a fixture, or a dedicated utility is missing.

A long test is usually a sign of weak boundaries.

The responsibility of the test is only to express:

1. the scenario being tested
2. the action being performed
3. the expected result

The test should not contain reusable engine logic.

If logic is useful for the runtime, CLI, examples, demos, or real rendering workflows, it must live in the proper production area, not in `tests/`.

If logic is useful only to reduce test repetition, it should live in a test helper or fixture file, not be repeated across many test cases.

Preferred structure:

```txt
tests/
  helpers/
    render_test_utils.hpp
    pixel_assertions.hpp
    scene_fixtures.hpp
    render_graph_fixtures.hpp

  core/
  scene/
  render_graph/
  renderer/
  effects/
  cache/
  io/
```

## Test Minimalism Rule

Tests must be minimal.

Every unnecessary line inside a test is a sign that the real code may be missing an API, helper, fixture, preset, or properly placed responsibility.

A test should not rebuild the engine from scratch. It should only describe the scenario, execute the behavior, and verify the result.

If the same setup appears in multiple tests, extract it.

If the setup represents real engine behavior, move it into production code.

If the setup exists only for testing, move it into `tests/helpers`.

Long tests are not a badge of completeness. Long tests are usually a symptom of poor boundaries.

The shorter and clearer the test, the healthier the architecture.
