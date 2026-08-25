# ExprTk spike — NO-GO

The ExprTk feasibility spike is closed without production migration.

Decision basis:

- the existing 130-case spike corpus does not cover the complete Chronon
  expression contract (`seedRandom`, `wiggle`, `posterizeTime`, `loopIn`,
  `loopOut`, and cross-layer resolution);
- ExprTk adds a very large header-only translation unit and materially worsens
  clean compile time, peak compiler memory, and binary size;
- the production parser already owns the Chronon-specific AE semantics and
  remains the lower-risk implementation for the current release.

Consequences:

- `ExpressionParser` remains the canonical backend;
- the ExprTk build option and spike target are removed;
- no ExprTk dependency is added to production or vcpkg;
- a future replacement must first provide full golden-corpus parity and an
  acceptable build/runtime budget.
