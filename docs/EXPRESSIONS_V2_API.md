# Expressions V2 — Public API Reference

> **Status:** Quarantined (Opzione A — `experimental/expressions/` under
> `CHRONON3D_BUILD_EXPERIMENTAL=OFF` by default). This document is the
> forward-looking spec for the engine's public surface and will lock with the
> engine's promotion merge out of quarantine per
> `docs/EXPRESSIONS_V2_PROMOTION.md` Gate 4.

This is the symbol-by-symbol reference for the Path B expression engine,
`chronon3d::expressions::v2`. Every public symbol that appears in
`experimental/expressions/include/chronon3d_experimental/expressions/v2/*.hpp`
is listed below, organised by header file. Headers appear in pipeline order —
`expression_value` is the leaf type, the rest of the pipeline consumes and
produces it.

Conventions used throughout this document:

- **Namespace.** Everything lives under `chronon3d::expressions::v2`. The
  AST factory functions additionally nest under `chronon3d::expressions::v2::ast`.
- **`[[nodiscard]]`.** Where the symbol is documented as `[[nodiscard]]`,
  the compiler enforces ignoring the return value; treat the return as
  mandatory. `inline` functions are header-defined and may be inlined into
  hot paths.
- **`noexcept`.** Where the symbol is documented as `noexcept`, no
  exceptions cross the boundary (callers can rely on stack-unwinding NOT
  occurring here). Errors are surfaced via out-parameter structs
  (`LexError`, `VmError`, `CompileError`, `TypeError`) so the engine never
  throws.
- **Lifetime / Storage.** All heap allocations carry a comment in this
  document so a reviewer can spot non-trivial allocator pressure without
  re-reading the header. Default = `value-typed, no heap allocation`.
- **Threading.** Default = single-threaded per `Vm` and per `Program`
  instance; compiling itself is pure and parallelisable. Specific
  exceptions are called out per-symbol.

---

## Index of headers

| Header | Role | Public symbols |
|---|---|---|
| `expression_value.hpp` | Leaf value type + factories | `LayerReference`, `PropertyReference`, `Color`, `ExpressionValue`, `ExpressionValueKind`, `kind()`, `as_*()`, `make_*()`, `to_string()` |
| `lexer.hpp` | Source → token stream | `SourceSpan`, `TokenKind`, `Token`, `LexError`, `LexResult`, `token_kind_name()`, `to_string()`, `lex()` |
| `ast.hpp` | AST node hierarchy + factories | `AstIdentifier`, `AstMemberAccess`, `AstIndexAccess`, `AstCall`, `BinaryOp`, `AstBinary`, `UnaryOp`, `AstUnary`, `AstConditional`, `AstNode`, `ast::make_*()` |
| `type_checker.hpp` | Static type analysis | `Type`, `TypeError`, `TypeCheckResult`, `type_name()`, `type_check()`, `is_vector_type()`, `is_numeric_type()` |
| `vm.hpp` | Stack-machine interpreter | `VmError`, `Vm` |
| `bytecode.hpp` | Op + pool encoding | `OpKind`, `Op`, `Program`, `op_kind_name()`, `pack_const_slot()`, `const_slot_tag()`, `const_slot_index()` |
| `compiler.hpp` | Source/AST → Program | `CompileError`, `CompileResult`, `compile()`, `compile_ast()` |
| `dependency_graph.hpp` | Static cycle detection | `NodeId`, `NodeIdHash`, `CycleReport`, `DependencyGraph` |

---

## `expression_value.hpp`

Path B's tagged-union value type plus opaque host-resolution references.

### `struct LayerReference`

```cpp
struct LayerReference {
    std::string layer_id;
    bool operator==(const LayerReference& o) const noexcept;
    bool operator!=(const LayerReference& o) const noexcept;
};
```

- **Purpose:** Opaque identifier for a "layer" in the host scene graph.
  Carries only `layer_id`; the host resolves the actual layer at runtime
  via the VM's env callbacks (`Vm::set` + a future `Vm::set_resolver`).
- **Lifetime:** Value-typed; `layer_id` owns its string storage.
- **Threading:** Copies of `LayerReference` are independent; comparison is
  value-typed and lock-free.

### `struct PropertyReference`

```cpp
struct PropertyReference {
    std::string layer_id;
    std::string property_path;  // dot-path inside the layer: "position.x"
    bool operator==(const PropertyReference& o) const noexcept;
    bool operator!=(const PropertyReference& o) const noexcept;
};
```

- **Purpose:** Opaque `layer_id + property_path` tuple. Combined host
  identity (which layer, which property of that layer).
- **Lifetime:** Value-typed; both strings owned.
- **Threading:** Same as `LayerReference`. Equality is value-typed and
  lock-free.

### `struct Color`

```cpp
struct Color {
    f32 r{0.0f};
    f32 g{0.0f};
    f32 b{0.0f};
    f32 a{1.0f};
    Color() = default;
    Color(f32 rr, f32 gg, f32 bb, f32 aa = 1.0f);
    bool operator==(const Color& o) const noexcept;
    bool operator!=(const Color& o) const noexcept;
};
```

- **Purpose:** RBBA color (additive over alpha). Maps directly to the
  product-side `chronon3d::Color` (the canonical type); v2 embeds an
  identical-by-spec mirror so the experimental headers don't pull the
  product-side `chronon3d::Color` into the quarantine.
- **Lifetime:** Value-typed (POD-friendly ctor); no heap allocation.
- **Ordering:** RGBA, identical to Path A's animated-value color.
- **Threading:** Value-typed; trivially copyable; lock-free.

### `using ExpressionValue`

```cpp
using ExpressionValue = std::variant<
    double,              // number
    bool,                // bool
    std::string,         // string
    Vec2, Vec3, Vec4,    // spatial vectors (from math/glm_types.hpp)
    Color,               // rgba color (the v2 mirror type above)
    LayerReference,      // opaque layer id
    PropertyReference    // opaque layer + prop
>;
```

- **Purpose:** The value space the VM can return and the env can store.
  Order is locked for ABI stability (alternative indices map 1:1 to
  `ExpressionValueKind`).
- **Lifetime:** Value-typed `std::variant`; same lifetime rules apply to
  each alternative. Strings inside the variant own their storage.
- **Threading:** A single `ExpressionValue` instance is not safe to mutate
  concurrently from multiple threads; copies are independent.
- **Invariant:** Order of alternatives is *locked*. Adding a new
  alternative is an ABI break; reordering is an ABI break.

### `enum class ExpressionValueKind : std::uint8_t`

```cpp
enum class ExpressionValueKind : std::uint8_t {
    Number = 0,
    Bool = 1,
    String = 2,
    Vec2 = 3,
    Vec3 = 4,
    Vec4 = 5,
    Color = 6,
    LayerReference = 7,
    PropertyReference = 8,
};
```

- **Purpose:** Cheap kind tag without visiting the variant (`v.index()`).
- **Lifetime:** Enum value (POD).
- **Invariant:** Numeric values match `ExpressionValue.index()` exactly;
  adding a new alternative requires updating both.

### `kind(const ExpressionValue&) noexcept`

```cpp
[[nodiscard]] inline ExpressionValueKind kind(const ExpressionValue& v) noexcept;
```

- **Purpose:** Returns the active alternative's kind tag.
- **Threading:** Lock-free; reads the variant's index.

### as-family coercion helpers

```cpp
[[nodiscard]] inline const double*            as_number(const ExpressionValue& v) noexcept;
[[nodiscard]] inline const bool*              as_bool  (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const std::string*       as_string(const ExpressionValue& v) noexcept;
[[nodiscard]] inline const Vec2*              as_vec2  (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const Vec3*              as_vec3  (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const Vec4*              as_vec4  (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const Color*             as_color (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const LayerReference*    as_layer (const ExpressionValue& v) noexcept;
[[nodiscard]] inline const PropertyReference* as_prop  (const ExpressionValue& v) noexcept;
```

- **Purpose:** `std::get_if<T>`-shaped coercion helpers; return
  `nullptr` (not `std::nullopt`) when the alternative doesn't match.
  Designed for zero-overhead hot-path probes.
- **Lifetime:** Pointer borrows the `ExpressionValue` storage; the pointer
  is invalidated when the `ExpressionValue` is destroyed or moves.
- **Threading:** Same as `kind()` — lock-free read of the variant.

### make-* constructors

```cpp
[[nodiscard]] inline ExpressionValue make_number(double d)                              noexcept;
[[nodiscard]] inline ExpressionValue make_bool  (bool   b)                              noexcept;
[[nodiscard]] inline ExpressionValue make_string(std::string s);
[[nodiscard]] inline ExpressionValue make_vec2  (Vec2   v)                              noexcept;
[[nodiscard]] inline ExpressionValue make_vec3  (Vec3   v)                              noexcept;
[[nodiscard]] inline ExpressionValue make_vec4  (Vec4   v)                              noexcept;
[[nodiscard]] inline ExpressionValue make_color (Color  c)                              noexcept;
[[nodiscard]] inline ExpressionValue make_layer (LayerReference r)                      noexcept;
[[nodiscard]] inline ExpressionValue make_property(PropertyReference p)                 noexcept;
```

- **Purpose:** Construct an `ExpressionValue` from a typed value without
  `std::visit`. `make_string` is the only non-`noexcept`-termed one
  because it moves the parameter (string constructors can throw
  `std::bad_alloc`).
- **Lifetime:** Each factory hands back an owning `ExpressionValue`.
- **Threading:** Same as `ExpressionValue` itself.

### `to_string(const ExpressionValue&)`

```cpp
[[nodiscard]] std::string to_string(const ExpressionValue& v);
```

- **Purpose:** Diagnostic — render any alternative as a string for logs.
  NOT a canonical-form guarantee; output is for human consumption.
- **Lifetime:** Resulting string owns its storage.
- **Threading:** Lock-free.

---

## `lexer.hpp`

Source string → token stream with span-tagged diagnostics.

### `struct SourceSpan`

```cpp
struct SourceSpan {
    std::uint32_t start{0};
    std::uint32_t end{0};
    std::uint32_t line{1};
    std::uint32_t col{1};
    [[nodiscard]] std::size_t length() const noexcept;
};
```

- **Purpose:** Byte range + line/column for diagnostics. Carried on EVERY
  AST/bytecode node so downstream stages can produce targeted error
  messages; line/column are 1-based; byte offsets are 0-based.
- **Lifetime:** Value-typed.
- **Invariant:** `end >= start` after `length()` returns 0 (defensive
  branch when offsets are inverted by bad upstream tooling).

### `enum class TokenKind : std::uint16_t`

```cpp
enum class TokenKind : std::uint16_t {
    Number, String, Identifier, BoolTrue, BoolFalse,
    LParen, RParen, LBracket, RBracket, Comma, Dot, Arrow,
    Plus, Minus, Star, Slash, Percent,
    EqEq, NotEq, Less, LessEq, Greater, GreaterEq,
    AndAnd, OrOr, Bang, Question, Colon,
    Eof,
};
```

- **Purpose:** Categorical token type. `Arrow` is reserved for future
  member-access syntax; token stream may contain `Arrow` but the parser
  does not bind it yet.
- **Lifetime:** Enum value (POD).
- **Invariant:** Numeric values are stable; reordering is an ABI break
  for any pre-compiled token stream cache.

### `token_kind_name(TokenKind) noexcept`

```cpp
[[nodiscard]] const char* token_kind_name(TokenKind k) noexcept;
```

- **Purpose:** Stable printable name for diagnostics/log lines. Pointer
  is to a static-string lifetime — caller does NOT own.

### `struct Token`

```cpp
struct Token {
    TokenKind     kind{TokenKind::Eof};
    SourceSpan    span{};
    std::string   lexeme;        // identifier / string literal body
    double        number_value{0.0};  // parsed value for Number literals
};
```

- **Purpose:** A single lexeme + its kind + its source span. For numeric
  literals, also stores the parsed `double` so the compiler doesn't have
  to re-parse.
- **Lifetime:** Value-typed; `lexeme` owns its string.

### `to_string(TokenKind)`

```cpp
[[nodiscard]] std::string to_string(TokenKind k);
```

- **Purpose:** Diagnostic — printable kind token (mirrors
  `token_kind_name` but returns a `std::string` for callers that want a
  concrete owning string).

### `struct LexError`

```cpp
struct LexError {
    SourceSpan span{};
    std::string message;
};
```

- **Purpose:** Diagnostic with source span; one per lex failure.
- **Lifetime:** Owning; safe to copy.

### `struct LexResult`

```cpp
struct LexResult {
    std::vector<Token> tokens;
    std::vector<LexError> errors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};
```

- **Purpose:** Lex output. `tokens` may contain partial/incomplete data
  on `!ok()`; callers should treat `tokens` as best-effort when errors
  are non-empty.
- **Lifetime:** `tokens` and `errors` own their strings.
- **Threading:** A `LexResult` is read-only after construction; multiple
  threads may consume it concurrently.

### `lex(std::string_view) noexcept`

```cpp
[[nodiscard]] LexResult lex(std::string_view source) noexcept;
```

- **Purpose:** Tokenise a source string. Entry point of the v2
  pipeline. Does NOT build an AST — that's `compile()`'s job.
- **Lifetime:** `source` is borrowed; `LexResult` owns its copies.
- **Threading:** Pure read of `source`; safe to run on many threads in
  parallel for many sources. The host must arrange its own sharing of
  `source` (typically: pass `std::string_view` over a long-lived
  `std::string`).

---

## `ast.hpp`

AST node hierarchy + factory sub-namespace.

### Cross-referencing node structs

```cpp
struct AstIdentifier   { std::string name; SourceSpan span{}; };
struct AstMemberAccess { std::unique_ptr<AstNode> base; std::string member; SourceSpan span{}; };
struct AstIndexAccess  { std::unique_ptr<AstNode> base; std::unique_ptr<AstNode> index; SourceSpan span{}; };
struct AstCall         { std::string name; std::vector<std::unique_ptr<AstNode>> args; SourceSpan span{}; };
struct AstBinary       { BinaryOp op; std::unique_ptr<AstNode> lhs; std::unique_ptr<AstNode> rhs; SourceSpan span{}; };
struct AstUnary        { UnaryOp op; std::unique_ptr<AstNode> operand; SourceSpan span{}; };
struct AstConditional  { std::unique_ptr<AstNode> condition; std::unique_ptr<AstNode> then_branch; std::unique_ptr<AstNode> else_branch; SourceSpan span{}; };
```

- **Purpose:** The cross-referencing branch of the AST. Each carries
  children via `std::unique_ptr<AstNode>` so `AstNode`'s full definition
  can come LATER in the file (canonical fix for the
  `struct AstNode;` forward-decl + `using AstNode = std::variant<...>`
  alias-conflict — header documents the rationale inline).
- **Lifetime:** Each `unique_ptr<AstNode>` owns its child. Default
  construction = empty; factories set the fields.
- **Threading:** Each AST instance is single-threaded for mutation; copies
  deep-clone via `unique_ptr` move/copy semantics.

### Op enums + AstBinary/AstUnary

```cpp
enum class BinaryOp : std::uint8_t { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or };
enum class UnaryOp  : std::uint8_t { Neg, Not };
```

- **Purpose:** Operator discrimination on binary/unary AST nodes.
- **Lifetime:** Enum values (POD).
- **Invariant:** Numeric order is stable; reordering is an ABI break.

### `struct AstNode : std::variant<...>`

```cpp
struct AstNode : std::variant<
    double,
    std::string,
    bool,
    AstIdentifier,
    AstMemberAccess,
    AstIndexAccess,
    AstCall,
    AstBinary,
    AstUnary,
    AstConditional
> {
    using variant::variant;
};
```

- **Purpose:** The AST root: an inheritance-style variant that lets us
  forward-declare `AstNode` AND have it act as a variant (see comment
  block in the header for the canonical cycle-resolution pattern).
- **Lifetime:** Value-typed, but children are owned via `unique_ptr`.
- **Invariant:** MUST stay non-polymorphic. Header enforces this at
  compile time via
  `static_assert(!std::is_polymorphic_v<AstNode>, ...)` and
  `static_assert(!std::has_virtual_destructor_v<AstNode>, ...)`.
  A contributor who adds a virtual function silently breaks polymorphic
  delete (UB) — these asserts catch it before merge.
- **Threading:** Single-threaded mutation; copies are deep (variant +
  unique_ptr trees).

### `ast::make_*` factory sub-namespace

```cpp
namespace ast {
[[nodiscard]] inline AstNode make_number(double d, SourceSpan s = {}) noexcept;
[[nodiscard]] inline AstNode make_string(std::string s);  // moves s
[[nodiscard]] inline AstNode make_bool(bool b);
[[nodiscard]] inline AstNode make_identifier(std::string name, SourceSpan s);
[[nodiscard]] inline AstNode make_member(AstNode base, std::string member, SourceSpan s);
[[nodiscard]] inline AstNode make_index(AstNode base, AstNode index, SourceSpan s);
[[nodiscard]] inline AstNode make_call(std::string name, std::vector<AstNode> args, SourceSpan s);
[[nodiscard]] inline AstNode make_binary(BinaryOp op, AstNode lhs, AstNode rhs, SourceSpan s);
[[nodiscard]] inline AstNode make_unary(UnaryOp op, AstNode operand, SourceSpan s);
[[nodiscard]] inline AstNode make_conditional(AstNode cond, AstNode t, AstNode e, SourceSpan s);
}
```

- **Purpose:** Unambiguous AST factory entry points. Lives in
  `chronon3d::expressions::v2::ast::` (different sub-namespace from the
  `expression_value.hpp` make-family) so callers don't cross-pollute
  `make_number` (an `AstNode` factory) with `make_number` (an
  `ExpressionValue` factory).
- **Lifetime:** Each returns an `AstNode` by value. Children are moved
  into place via `std::make_unique<AstNode>`.
- **Threading:** Same as `AstNode`.

---

## `type_checker.hpp`

Static type analysis. Permissive on the `Type::Top` family (free
identifiers, unresolved layer refs); strict on asymmetric arithmetic
between fully-inferred types.

### `enum class Type : std::uint8_t`

```cpp
enum class Type : std::uint8_t {
    Unknown = 0,
    Number, String, Bool, Vec2, Vec3, Vec4, Color,
    LayerReference, PropertyReference,
    Top,            // permissive fallback: any compatible type
};
```

- **Purpose:** Static type tag. `Top` is the "I don't know yet" fallback
  used for free identifiers (see header comment block for the
  trade-off rationale).
- **Lifetime:** Enum value (POD).
- **Invariant:** Numeric order is stable.

### `type_name(Type) noexcept`

```cpp
[[nodiscard]] const char* type_name(Type t) noexcept;
```

- **Purpose:** Stable printable name for diagnostics.

### `struct TypeError`

```cpp
struct TypeError {
    SourceSpan span{};
    std::string message;
};
```

- **Purpose:** Diagnostic — one per static type-check failure.

### `struct TypeCheckResult`

```cpp
struct TypeCheckResult {
    Type root_type{Type::Unknown};
    std::vector<TypeError> errors;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};
```

- **Purpose:** Type-check output. `root_type` is the inferred type of
  the AST root; `errors` lists static failures.

### `type_check(const AstNode&) noexcept`

```cpp
[[nodiscard]] TypeCheckResult type_check(const AstNode& root) noexcept;
```

- **Purpose:** Traverse the AST and emit a per-node inferred type plus
  diagnostics for asymmetric arithmetic.
- **Lifetime:** Borrows the AST; result owns its diagnostics.
- **Threading:** Pure read of the AST; safe to run on many threads in
  parallel for different ASTs. Same AST must not be mutated concurrently.

### `is_vector_type(Type) noexcept`, `is_numeric_type(Type) noexcept`

```cpp
[[nodiscard]] bool is_vector_type(Type t) noexcept;
[[nodiscard]] bool is_numeric_type(Type t) noexcept;
```

- **Purpose:** Helper predicates exposed for tests and downstream tools
  that need to reason about which alternatives are numeric vs vector.
  `is_numeric_type` returns true for `Type::Number` only (does NOT
  include `Vec*`, which are spatial types, not scalar).

---

## `vm.hpp`

Stack-machine interpreter. The engine's runtime evaluation context.

### `struct VmError`

```cpp
struct VmError {
    std::uint32_t pc{0};
    std::string message;
};
```

- **Purpose:** Diagnostic — `pc` is the program counter at the failing
  instruction; `message` is human-readable. One per Vm execution failure.

### `class Vm`

```cpp
class Vm {
public:
    void set(const std::string& name, ExpressionValue v);                          // bind a name to a value
    [[nodiscard]] std::optional<ExpressionValue> get(const std::string& name) const;
    void reset() noexcept;                                                          // see Owner-invariant below
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t env_size() const noexcept;
    [[nodiscard]] ExpressionValue run(const Program& program, VmError* err = nullptr);
    [[nodiscard]] static ExpressionValue evaluate(std::string_view source, VmError* err = nullptr);
private:
    std::unordered_map<std::string, ExpressionValue> env_;
};
```

#### `set(name, value)`

- **Purpose:** Bind `name` to `value` so a subsequent `LOAD_VAR name` op
  produces `value` when run.
- **Lifetime:** `name` and `value` are moved/copied into `env_`.
- **Threading:** Mutates `env_`; not safe for concurrent modification.
  Calling `set` from multiple threads on the same `Vm` requires external
  synchronisation.

#### `get(name) -> std::optional<ExpressionValue>`

- **Purpose:** Read back a previously `set` binding; returns
  `std::nullopt` if absent.
- **Lifetime:** Returns a fresh copy of the bound value.
- **Threading:** Lock-free read of `env_` is safe when no concurrent
  `set`/`reset` is in flight.

#### `reset() noexcept`

- **Purpose:** Clear all bindings so the Vm is in the same observable
  state as a freshly constructed Vm.
- **Lifetime:** In-place; mutates `env_`.
- **Threading:** Mutates `env_`; not safe for concurrent modification.
- **Owner-invariant (DO NOT REGRESS — Gate 2 contract):** `Vm` owns
  only `env_` as observable instance state today. If a future
  contributor adds a new private field with observable state (e.g.
  a const-pool cache, a stack snapshot for unwinding, a profiling
  accumulator), they MUST update `reset()` to clear every observable
  field, otherwise the Gate 2 determinism contract silently breaks.
  Required by Opzione B Gate 2 so test runs can isolate runs that
  share a long-lived Vm instance (anim systems often hold one across
  frames).

#### `empty()`, `env_size()`

- **Purpose:** Test-side guards before reusing a Vm (`empty()` true
  immediately after construction and after `reset()`).
- **Threading:** Lock-free reads.

#### `run(program, err)`

- **Purpose:** Execute `Program::code[pc=0..end]`. Returns the
  top-of-stack after `RETURN`. On error, populates `err` (if non-null)
  and returns a default-constructed `ExpressionValue` (variant alt
  0 = `double 0.0`).
- **Lifetime:** `program` is borrowed; the Vm does NOT copy it. Errors
  via `err` populate a fresh `VmError`.
- **Threading:** Mutates `pc`, `stack`, and may call `env_`. Two
  threads on the same Vm are UB; one Vm per thread is the canonical
  pattern.

#### `evaluate(source, err)` (static)

- **Purpose:** Convenience — compile + run. Useful for tests and one-shot
  evaluations that don't need to cache the bytecode.
- **Lifetime:** `source` is borrowed; the function owns the temporary
  `Program` internally.
- **Threading:** Same as `run`; the static function creates a temporary
  Vm internally, so each call is independent on the stack.

---

## `bytecode.hpp`

Op encoding + program-level pool tables.

### `enum class OpKind : std::uint8_t`

```cpp
enum class OpKind : std::uint8_t {
    LOAD_CONST = 0, LOAD_VAR = 1, STORE_VAR = 2,
    ADD=3, SUB=4, MUL=5, DIV=6, MOD=7, NEG=8,
    EQ=9, NE=10, LT=11, LE=12, GT=13, GE=14,
    AND=15, OR=16, NOT=17,
    MEMBER=18, INDEX=19, CALL=20,
    JMP=21, JMP_IF_FALSE=22, JMP_IF_TRUE=23, RETURN=24,
};
```

- **Purpose:** Categorical VM instruction. Every instruction carries a
  single operand (`Op::slot`); the operand's interpretation depends on
  the `OpKind`.
- **Lifetime:** Enum value (POD).
- **Invariant:** Numeric order is stable.

### `op_kind_name(OpKind) noexcept`

```cpp
[[nodiscard]] const char* op_kind_name(OpKind k) noexcept;
```

- **Purpose:** Stable printable name for diagnostics/disassembly.

### `struct Op`

```cpp
struct Op {
    OpKind              kind{OpKind::RETURN};
    std::uint32_t       slot{0};
};
```

- **Purpose:** A single VM instruction. `slot` is the operand; for
  `LOAD_CONST`, the high byte tags the constant pool type (see
  `pack_const_slot`); otherwise `slot` is a name-pool index.
- **Lifetime:** Value-typed (POD).

### `struct Program`

```cpp
struct Program {
    std::vector<Op>             code;
    std::vector<double>         const_numbers;
    std::vector<std::string>    const_strings;
    std::vector<bool>           const_bools;
    std::vector<std::string>    names;
    [[nodiscard]] bool   empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
};
```

- **Purpose:** Fully-resolved bytecode ready for the VM to interpret;
  pool tables hold literal constants and interned names.
- **Lifetime:** Owns its vectors. Strings are owned.
- **Threading:** A `Program` is read-only after emission; multiple
  threads may safely call `Vm::run` on copies of it concurrently as
  long as each thread has its own `Vm`.
- **Mutation:** Mutating a `Program` while a `Vm` runs it is UB.

### `pack_const_slot` / `const_slot_tag` / `const_slot_index`

```cpp
[[nodiscard]] inline std::uint32_t pack_const_slot      (std::uint8_t tag, std::uint32_t idx) noexcept;
[[nodiscard]] inline std::uint8_t  const_slot_tag       (std::uint32_t slot) noexcept;
[[nodiscard]] inline std::uint32_t const_slot_index     (std::uint32_t slot) noexcept;
```

- **Purpose:** Bit-pack helpers for `LOAD_CONST`'s slot operand. The
  high byte is the pool type tag (0 = number, 1 = string, 2 = bool),
  the low 24 bits are the pool index. Sharing the slot encoding lets
  `LOAD_CONST` reference any pool via a single `Op::slot` operand.
- **Invariant:** Tag must fit in 8 bits; index must fit in 24 bits
  (≈16M constants per program — not a real-world limit).

---

## `compiler.hpp`

Source/AST → bytecode. Single entry point for the compile pipeline.

### `struct CompileError`

```cpp
struct CompileError {
    SourceSpan span{};
    std::string message;
};
```

- **Purpose:** Diagnostic — one per compile-stage failure. Failures can
  come from lex, parse, type-check, or emit.

### `struct CompileResult`

```cpp
struct CompileResult {
    Program                    program;
    std::vector<CompileError>  errors;
    AstNode                    ast;
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};
```

- **Purpose:** Compile output. `ast` is the parsed AST rooted at the
  source's top-level expression; default-constructed (variant alt 0 =
  `double 0.0`) when source failed at lex/parse. `program` is the
  emitted bytecode — `code` may be partial/incomplete on `!ok()`.

### `compile(source) noexcept`

```cpp
[[nodiscard]] CompileResult compile(std::string_view source) noexcept;
```

- **Purpose:** Run the full pipeline (lex → parse → type-check → emit)
  from source string to ready-to-run `Program`.
- **Lifetime:** `source` is borrowed; `CompileResult` owns its copies.
- **Threading:** Pure read of `source`; safe to run on many threads in
  parallel for many sources. Output is owned and safe to share across
  threads for read-only consumption.

### `compile_ast(ast) noexcept`

```cpp
[[nodiscard]] CompileResult compile_ast(const AstNode& ast) noexcept;
```

- **Purpose:** Skip lex/parse; emit bytecode from a pre-built AST. Used
  by callers that already have an AST (e.g. integration with the existing
  AnimatedValue expression-string path defined in TICKET-EXP2-G3).
- **Lifetime:** `ast` is borrowed; `CompileResult` owns its copies.
- **Threading:** Same as `compile()`.

---

## `dependency_graph.hpp`

Static cycle detection over a scene's set of compiled Programs.

### `struct NodeId` / `struct NodeIdHash`

```cpp
struct NodeId {
    std::string name;
    bool operator==(const NodeId& o) const noexcept;
    bool operator!=(const NodeId& o) const noexcept;
};
struct NodeIdHash {
    size_t operator()(const NodeId& n) const noexcept;
};
```

- **Purpose:** Stable per-expression identifier for the scene graph.
  Hashable so the dependency graph can use it as an unordered-map key.
- **Lifetime:** `name` owns its storage.
- **Threading:** Copies independent; the hash functor is lock-free.

### `struct CycleReport`

```cpp
struct CycleReport {
    std::vector<NodeId> cycle_nodes;
};
```

- **Purpose:** Diagnostic — names the nodes that form a cycle. `cycle_nodes`
  is one example cycle; the graph builder may report any cycles present.

### `class DependencyGraph`

```cpp
class DependencyGraph {
public:
    void   add_program(const NodeId& id, const Program& program);
    void   add_writer  (const NodeId& writer, const std::string& var_name);
    void   add_edge    (const NodeId& from, const NodeId& to);
    std::vector<NodeId> topological_order(CycleReport& report);
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] bool        empty()      const noexcept;
private:
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> reads_;
    std::unordered_map<NodeId, std::vector<std::string>, NodeIdHash> writes_;
    std::unordered_map<std::string, NodeId, std::hash<std::string>> var_to_writer_;
    std::unordered_set<NodeId, NodeIdHash> nodes_;
};
```

#### `add_program(id, program)`

- **Purpose:** Auto-collect `id`'s reads via the program's `LOAD_VAR` /
  `STORE_VAR` instructions. (Writes must be added separately via
  `add_writer` — the program walk only sees its own reads.)
- **Lifetime:** `id` and `program` are borrowed.
- **Threading:** Mutates internal maps; not safe for concurrent calls
  without external synchronisation.

#### `add_writer(writer, var_name)` / `add_edge(from, to)`

- **Purpose:** Manually declare a write or an explicit edge. Use these to
  fill the gap left by the program walk (writes) and to declare edges
  the graph wouldn't otherwise discover.
- **Threading:** Same as `add_program`.

#### `topological_order(report)`

- **Purpose:** Kahn's-style top-sort over the known nodes/edges. Returns
  the nodes in evaluation order; the input `CycleReport` is populated
  with at least one cycle if any are present.
- **Lifetime:** Returns an owning vector of `NodeId` copies. `report` is
  populated in-place.
- **Threading:** Marks internal state as "consumed for ordering"; calling
  it twice without re-adding is well-defined (same output) but loses
  the cycle report from the first call.

#### `node_count()`, `empty()`

- **Purpose:** Test-side guards.

#### Scope boundary (Gate 3 carve-out)

`DependencyGraph` is **`expression-level`** cycle-aware: the graph Path B
builds from bytecode catches cycles between operations on the same
program or where one program's `LOAD_VAR` reads another's `STORE_VAR`
via the supplementary `add_writer`/`add_edge` calls.

**Cross-layer cycles** — e.g. `value` referring transitively back to
itself through another `layer("X").prop` — are a host-side concern
resolved at runtime via `Vm::set_resolver`, and **remain out of scope**
for Gate 3 (per TICKET-EXP2-G3 delegation-design (e) and the Feature
Parity/Gate 8 contracts). They are not detected by `DependencyGraph`
alone; the host must break them upstream or refuse to evaluate.

---

## Cross-header invariants worth grepping for

- `[[nodiscard]]` on all observability-returning symbols (engine
  contract: silent dropping = bug).
- `noexcept` on all lex-/kind-query-/tag-emission symbols (engine
  contract: never throw across the boundary).
- `OpKind`, `TokenKind`, `Type`, `ExpressionValueKind`, `BinaryOp`,
  `UnaryOp` — all 6 of these enums are POD with **stable numeric
  order**. Reordering is an ABI break; pre-compiled token streams,
  bytecode blobs, and binary representations of `Program::code` all
  encode them.
- No virtual functions or virtual destructors on `Vm`, `Program`,
  `DependencyGraph`, `AstNode`. Header enforces this for `AstNode`
  via static_assert; the other three rely on convention.

---

## Usage examples

A small set of end-to-end examples showing how the engine's surface
fits together. These mirror the trace laid out in
`docs/EXPRESSIONS_V2_PIPELINE.md` §"End-to-end trace" so a reader can
follow the same flow against both docs in parallel.

### 1. Compile + run end-to-end

```cpp
#include <chronon3d_experimental/expressions/v2/compiler.hpp>
#include <chronon3d_experimental/expressions/v2/expression_value.hpp>
#include <chronon3d_experimental/expressions/v2/vm.hpp>

using namespace chronon3d::expressions::v2;

CompileResult cr = compile("value * 2 + 1");
if (!cr.ok()) {
    /* handle compile errors via cr.errors */
}

Vm vm;
vm.set("value", make_number(7.0));

VmError err;
ExpressionValue result = vm.run(cr.program, &err);
if (!err.message.empty()) { /* handle runtime error */ }
/* result is make_number(15.0) */
```

### 2. End-to-end via `Vm::evaluate` (one-shot)

```cpp
#include <chronon3d_experimental/expressions/v2/vm.hpp>

using namespace chronon3d::expressions::v2;

VmError err;
ExpressionValue result = Vm::evaluate("1 + 2 * 3", &err);
/* result is make_number(7.0). Operator precedence: `*` binds tighter
   than `+` — the parser emits `(2 * 3) + 1` semantically. */
```

### 3. AST-level — inspect parse shape

```cpp
#include <chronon3d_experimental/expressions/v2/ast.hpp>
#include <chronon3d_experimental/expressions/v2/compiler.hpp>

using namespace chronon3d::expressions::v2;

CompileResult cr = compile("a + b * c");
if (!cr.ok()) return;
/* cr.ast is the root of the parsed AST. Test setups use
   cr.ast directly to verify parse-stage behaviour without
   going through the VM.  */
```

### 4. Lex-level — tokenise for inspection

```cpp
#include <chronon3d_experimental/expressions/v2/lexer.hpp>

using namespace chronon3d::expressions::v2;

LexResult lr = ::lex("value * 2 + 1");
if (!lr.ok()) { /* surface lr.errors */ }
/* lr.tokens iterates Identifier("value"), Star, Number(2.0), Plus,
   Number(1.0), Eof. */
```

### 5. Type-check — surface static-type errors before emit

```cpp
#include <chronon3d_experimental/expressions/v2/ast.hpp>
#include <chronon3d_experimental/expressions/v2/type_checker.hpp>

using namespace chronon3d::expressions::v2;

CompileResult cr = compile("1 + \"foo\"");  // asymmetric Number + String
TypeCheckResult tr = ::type_check(cr.ast);
/* tr.ok() is false; tr.errors contains a TypeError pointing at the
   `+` operator. */
/* Compare with the permissive path: compile("true + x") where x is a
   free identifier — tr.ok() is true and tr.root_type is Top. */
```

### 6. Dependency graph — detect cycles across many expressions

```cpp
#include <chronon3d_experimental/expressions/v2/compiler.hpp>
#include <chronon3d_experimental/expressions/v2/dependency_graph.hpp>

using namespace chronon3d::expressions::v2;

DependencyGraph graph;
graph.add_program(NodeId{"alpha"}, compile("beta + 1").program);
graph.add_program(NodeId{"beta"},  compile("alpha + 1").program);
graph.add_writer(NodeId{"alpha"},  "y");
graph.add_writer(NodeId{"beta"},   "x");

/* alpha writes y, beta reads y and writes x; alpha reads x, so they
   mutually depend. topologically_order will surface a CycleReport
   containing cycle_nodes = {alpha, beta}. */
CycleReport report;
auto order = graph.topological_order(report);
/* order is empty; report.cycle_nodes is populated. */
```

### 7. Anim-system pattern — long-lived Vm with `reset()`

```cpp
#include <chronon3d_experimental/expressions/v2/vm.hpp>

using namespace chronon3d::expressions::v2;

/* AnimatedValue per-frame evaluation pattern. */
Vm vm;
Program p = compile("value * 2 + 1").program;

for (Frame f : frames) {
    vm.reset();                                              // Gate 2 determinism
    vm.set("value", f.base_value);                           // per-frame env
    vm.set("time",  make_number(f.time));                    // AE-standard

    VmError err;
    ExpressionValue result = vm.run(p, &err);
    /* emit result to GPU; empty(err.message) for the happy path */
}
```

This seven-step sequence — compile, evaluate, AST inspection, lex
inspection, type-check, cycle detection, and anim-pool reuse — is
sufficient for a new contributor to interact with the engine's
entire surface if they want to onboard from examples alone
(skipping the per-header detail reference above).
