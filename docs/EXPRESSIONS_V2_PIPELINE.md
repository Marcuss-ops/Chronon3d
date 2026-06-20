# Expressions V2 — Pipeline Reference

> **Status:** Quarantined (Opzione A — `experimental/expressions/` under
> `CHRONON3D_BUILD_EXPERIMENTAL=OFF` by default). This document is the
> forward-looking onboarding reference for the engine's compile/run
> pipeline and will lock with the engine's promotion merge out of
> quarantine per `docs/EXPRESSIONS_V2_PROMOTION.md` Gate 4.

This is the pipeline reference for the Path B expression engine,
`chronon3d::expressions::v2`. It is the doc a new contributor reads
*before* opening the compiler source — by the end they should be able
to predict what stage a given failure surfaced from and roughly what
data shape that stage emits.

The pipeline is intentionally layered: each stage has a tightly-typed
input/output contract and a list of error categories, so a failure can
be reproduced by re-running only the stage that produced it (e.g. a
parse-stage regression will surface in `CompileResult::errors` with a
`SourceSpan` pointing at the offending token).

The companion reference for the engine's public API surfaces is
`docs/EXPRESSIONS_V2_API.md`.

---

## End-to-end diagram

```
                            ┌───────────────────────────────────┐
                            │   compile(source: string_view)    │  ← entry point
                            └────────────────┬──────────────────┘
                                             │
                                             ▼
   ┌─────────────────┐    ┌─────────────────┴─────────────────┐
   │   lex(source)   │ ◄──┤   Stage 1 — Lex                   │
   │ → LexResult     │    │   tokens + LexError[]             │
   └────────┬────────┘    └────────────────┬───────────────────┘
            │                              │
            ▼                              │
   ┌─────────────────┐                     │
   │  implicit       │ ◄──── Parse (private) ────────────┐
   │  parser         │                                   │
   │ → AstNode tree  │                                   │
   └────────┬────────┘                                   │
            │                                            │
            ▼                                            │
   ┌─────────────────┐    ┌─────────────────┬───────────┴────┐
   │ type_check(ast) │ ◄──┤  Stage 2 — Type-check            │
   │ → TypeCheckResult│   │  TypeCheckResult{root_type,      │
   │                 │    │    TypeError[]}                  │
   └────────┬────────┘    └─────────────────┬─────────────────┘
            │                              │
            ▼                              │
   ┌─────────────────┐                     │
   │ emit(ast)       │ ◄──── Emit (private) ─────────────── ┐
   │ → Program       │                                       │
   │   {code, pools} │                                       │
   └────────┬────────┘                                       │
            │                                                │
            ▼                                                │
   ┌─────────────────┐    ┌─────────────────┬────────────────┴──┐
   │  Vm::run(prog)  │ ◄──┤  Stage 3 — VM runtime              │
   │  → ExpressionValue │  │  stack-machine interpretation     │
   │  (or VmError)   │    │  env_ table for LOAD_VAR lookup   │
   └─────────────────┘    └────────────────────────────────────┘
```

Concurrency note: each stage is pure with respect to its inputs.
`compile(source)` is safe to run on many threads in parallel for many
sources. `Vm` and `Program` instances are single-threaded for runtime;
the canonical pattern is one Vm per thread, sharing `Program` copies
read-only.

The `ast` shown alongside `program` in `CompileResult` is the parsed
AST exposed for tests. Production callers should ignore `ast` and
use `program` directly.

---

## Stage 1 — Lexer

### Entry point

```cpp
#include <chronon3d_experimental/expressions/v2/lexer.hpp>
chronon3d::expressions::v2::LexResult r = ::lex(source);
```

### Input

`std::string_view` over the source text. The lexer reads but does not
own the bytes; the caller is responsible for keeping the underlying
storage alive for the duration of the call.

### Output

`LexResult { std::vector<Token> tokens; std::vector<LexError> errors; }`

Each `Token` carries `kind`, `span`, `lexeme`, and — for numeric
literals — the already-parsed `double number_value` so the compiler
does not have to re-parse.

### Failure modes

- Unterminated string literal — `LexError { span, "unterminated \"...\" literal" }`
- Unrecognised character — `LexError { span, "unexpected character `X`" }`
- Numeric overflow — `LexError { span, "numeric literal overflow" }`

`tokens` may still contain partial data on `!ok()`. Callers should
treat it as best-effort and surface `errors` first.

### Invariants worth knowing

- `Token::span` is always set; even for synthesised tokens (e.g. EOF)
  the span points at the end of the source.
- Numeric literals are always stored as `double`; integer-only paths
  go through the same slotpack encoding in the bytecode stage.
- `TokenKind::Arrow` (`->`) is reserved for future member-access
  syntax. Token streams may contain it; the parser does not bind it
  yet.

---

## Stage 1.5 — Parser (private)

### Entry point

There is no public `parse()` function — parsing is a private
implementation detail of `compile()`. The output AST is exposed via
`CompileResult::ast` so tests can verify parse-stage behaviour
without re-compiling.

### Input

`std::vector<Token>` from stage 1.

### Output

`AstNode` (rooted AST). Sibling structs (`AstIdentifier`,
`AstMemberAccess`, `AstIndexAccess`, `AstCall`, `AstBinary`,
`AstUnary`, `AstConditional`) carry children via
`std::unique_ptr<AstNode>` so the cycle-resolution pattern from
`ast.hpp` (forward-decl `AstNode`, late-definition as
`std::variant`-inheritance) can hold.

### Failure modes

- Mismatched parentheses — `CompileError { span, "expected `)`" }`
- Unexpected EOF — `CompileError { span, "unexpected EOF after `+`" }`
- Bad call arity (e.g. comma at end of arg list) — propagated from
  `AstCall` validation.

### Invariants worth knowing

- **Right-associativity:** `a ? b : c ? d : e` parses as
  `a ? b : (c ? d : e)`. The determinism test in
  `experimental/expressions/tests/test_expressions_v2_determinism.cpp`
  Section D pins this in place (see Gate 2 audit log).
- **Default-constructed `ast` = variant alt 0 (`double 0.0`).** This
  is the sentinel for failed lex/parse. A test should never
  `std::get<>` without first checking `CompileResult::ok()`.

---

## Stage 2 — Type-checker

### Entry point

```cpp
#include <chronon3d_experimental/expressions/v2/type_checker.hpp>
chronon3d::expressions::v2::TypeCheckResult r = ::type_check(ast);
```

There is also no public `type_check` call inside `compile()` apart
from the implicit stage. The type-checker is exposed for tests and
for tools that want to surface static-type errors before emitting.

### Input

`const AstNode&` (borrows).

### Output

`TypeCheckResult { Type root_type; std::vector<TypeError> errors; }`

`root_type` is the inferred type of the AST root. If the AST is
type-correct, it matches the runtime type the VM will eventually
return (modulo `Type::Top` permissiveness — see below).

### Failure modes

- Asymmetric arithmetic on fully-inferred types — e.g.
  `1 + "foo"` → `TypeError { span, "operator + not defined for Number + String" }`.
- Calling a non-callable identifier as a function — e.g.
  `value(1)` when `value` is bound to a number → `TypeError`.

### Invariants worth knowing — the `Type::Top` permissiveness

The header documents a specific trade-off: Path B accepts
expressions where at least one operand is `Type::Top` and emits
`Top` rather than an error. The intuition: After Effects-style
expressions frequently reference names whose effective type is
only known at runtime (free property bindings, `thisComp`-style
member chains through a layer ref, unresolved index lookups, etc.).
A strict reject on `Type::Top` operands would force every
host-authored expression to be explicitly refined upstream — not
viable.

Concretely (the four examples prefixed "Concretely" at the top of `type_checker.hpp`, plus two adjacent cases the header documents in prose — `thisLayer.position + thisLayer.scale` widening to `Vec3`, and `position * 2` where `position` is unbound so its static type is `Top`):

| Source expression | `type_check` result |
|---|---|
| `1 + "foo"` | ERROR (both operands fully inferred) |
| `true + x` | `Top` (Bool relaxed by free identifier `x`) |
| `"" + x` | `Top` (String relaxed by free identifier `x`) |
| `x * 2` | `Top` (Top relaxed by Number — happy-path variable binding) |
| `position + scale` | `Vec3` (vector+vector widens via `widen()`-style same-type clause) |
| `position * 2` (where `position` is unbound so its static type is `Top`) | `Top` (Number+Vector; lands in the `Top`-or-`Top` branch because at least one operand is `Top`) |

The runtime diagnostic still fires if the actual value's type is
incompatible — the static type-check is a permissive first pass, not
the final word.

---

## Stage 2.5 — Emit (private)

### Entry point

There is no public `emit()` function — bytecode emission is private to
`compile()`. The output `Program` is the public surface.

### Input

`const AstNode&` (the typed AST).

### Output

`Program { code; const_numbers; const_strings; const_bools; names; }`

`code` is the VM instruction stream; `Op::slot` is an operand that
the VM resolves at runtime (name-pool index for `LOAD_VAR`/`CALL`/
`MEMBER`; types-tagged constant-pool index for `LOAD_CONST` via
`pack_const_slot`).

### Failure modes

Emit does NOT fail on its own — by the time we get here, lex/parse/
type-check have already pruned the AST. Any "impossible" shape
(const-pool overflow, name-pool overflow) triggers an internal
assertion in debug builds or a deterministic runtime VMError in
release.

### Invariants worth knowing

- `Program` is *not thread-safe* while emitting, but is read-only
  after `compile()` returns. Multiple threads may share `Program`
  copies for read-only consumption.
- The `names` pool is interned: `LOAD_VAR foo` and `LOAD_VAR foo`
  within the same program share the same name-pool slot.

---

## Stage 3 — VM

### Entry point

```cpp
#include <chronon3d_experimental/expressions/v2/vm.hpp>
chronon3d::expressions::v2::Vm vm;
vm.set("value", ::make_number(7.0));
chronon3d::expressions::v2::VmError err;
chronon3d::expressions::v2::ExpressionValue r = vm.run(program, &err);
```

Or end-to-end:

```cpp
chronon3d::expressions::v2::ExpressionValue r =
    chronon3d::expressions::v2::Vm::evaluate(source, &err);
```

### Input

`const Program&` + `Vm&`'s env. The VM executes `Program::code[pc=0..end]`.

### Output

`ExpressionValue` — the top-of-stack after `RETURN`. On failure,
`VmError { pc, message }` is populated and the returned `ExpressionValue`
is the default `double 0.0`.

### Failure modes

- `LOAD_VAR name` where `name` is missing from `env_` →
  `VmError { pc, "LOAD_VAR: undefined name `name`" }`.
- Stack underflow on a binary op →
  `VmError { pc, "<op_kind_name> requires two operands on stack" }`.
- `CALL foo`, arity mismatch → propagates from the runtime
  builtin registry (see TICKET-EXP2-G3 Gate 3a for the YES/DEFER
  lookup).

### Invariants worth knowing — Gate 2 determinism contract

`Vm` owns ONLY `env_` as observable instance state today (per the
`reset()` owner-invariant documented in `docs/EXPRESSIONS_V2_API.md`).
If a future contributor adds observable state (e.g. a const-pool
cache, a stack snapshot, a profiling accumulator), they MUST update
`reset()` to clear it; otherwise the determinism contract is
silently broken.

The two practical patterns a host should adopt:

1. **Per-call Vm (canonical for animations).** Construct a fresh
   `Vm` per `evaluate()` call. Reset between calls is implicitly
   handled. No risk of state leakage.

2. **Long-lived Vm with `reset()` (canonical for test determinism
   and anim-system pooling).** Construct once, reuse across many
   frames, call `reset()` at isolation boundaries. The Gate 2
   determinism suite in
   `experimental/expressions/tests/test_expressions_v2_determinism.cpp`
   verifies the second pattern.

---

## End-to-end trace — `value * 2 + 1` given `value=7`

| Stage | Input | Output |
|---|---|---|
| (host) | source = `"value * 2 + 1"`; `Vm::set("value", 7.0)` | — |
| Lex | `"value * 2 + 1"` | `tokens = [Identifier("value"), Star, Number(2), Plus, Number(1), Eof]` |
| Parse | tokens | `ast = AstBinary{Add, AstBinary{Mul, Identifier("value"), Number(2)}, Number(1)}` |
| Type-check | ast | `root_type = Number` (no errors; both operands fully inferred) |
| Emit | ast | `program.code = [LOAD_VAR slot("value"), LOAD_CONST pack_const_slot(0, 0), MUL slot(0), LOAD_CONST pack_const_slot(0, 1), ADD slot(0), RETURN]` |
| VM run | program, env | `r = make_number(15.0)` |

Step-by-step VM stack trace:

```
pc=0  LOAD_VAR "value"           → push ExpressionValue(7.0)
pc=1  LOAD_CONST 0 (number=2.0)  → push ExpressionValue(2.0)
pc=2  MUL                        → pop 2, pop 7, push 14.0
pc=3  LOAD_CONST 1 (number=1.0)  → push ExpressionValue(1.0)
pc=4  ADD                        → pop 1, pop 14, push 15.0
pc=5  RETURN                     → top-of-stack = 15.0 → result
```

---

## Notes for new contributors

1. **Work on the smallest stage that surfaces the bug.** A parse-stage
   regression (e.g. left-associativity creep in the ternary) shows up
   in `CompileResult::errors` with a `SourceSpan` pointing at the
   offending token — re-run `lex(source)` standalone, then
   `type_check(parsed_ast)` to isolate.

2. **The cross-layer cycle problem is NOT detected by the
   `DependencyGraph`'s bytecode walk alone.** See `docs/EXPRESSIONS_V2_API.md`
   `Scope boundary (Gate 3 carve-out)` — the graph catches cycles
   within the `LOAD_VAR`/`STORE_VAR` reachability of the supplied
   programs, but a layer reference that transitively points back to
   itself (`value -> layer("X").prop -> value`) is a host concern
   and breaks at runtime via `Vm::set_resolver` rather than at
   graph-build time.

3. **`Program` is immutable after emission.** Mutating `code` while a
   `Vm` runs it is undefined. To re-emit, build a new `Program` via
   `compile()` / `compile_ast()`.

4. **`AstNode` MUST stay non-polymorphic.** The header enforces this
   with `static_assert(!std::is_polymorphic_v<AstNode>, ...)`. If
   you find yourself wanting to add a virtual function to
   `AstNode`, the answer is `std::visit` on the contained variant
   into a visitor pattern — the `using std::variant::variant;`
   `AstNode` in `ast.hpp` is already wired for that.

5. **New alternative = ABI break.** `OpKind`, `TokenKind`, `Type`,
   `ExpressionValueKind`, `BinaryOp`, `UnaryOp` are all POD with
   *stable numeric order*. Adding or reordering alternatives breaks
   any pre-compiled bytecode cache and any on-disk token stream
   log. Default to adding at the END and bumping a major version
   tag.
