# Wave 7-9 compiler/runtime boundary debt

Status: ACTIVE

Owner: Chronon compiler/runtime maintainers

The new compiler IR and runtime lifetime types are introduced while the legacy
plan compiler and prepared execution artifact remain available for output
parity. This bridge is intentional migration debt, not a second public IR.

Exit condition: compiler golden snapshots and legacy execution output are
identical for text, transition, precomposition, and video plans; all callers
use `Compiler` and the staged frontend/lowering pipeline.

Removal scope: delete the legacy compiler authorities and the private prepared
plan bridge only after the equivalence gate is green. No Wave 7-9 change may
delete those authorities prematurely.
