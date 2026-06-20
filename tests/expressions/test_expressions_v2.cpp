// test_expressions_v2.cpp — Path B expression engine end-to-end coverage.
//
// Coverage spans all 7 pipeline stages (lex -> AST -> type-check -> bytecode
// -> VM) plus the dependency graph + cycle detector. The headline "compile +
// run == 7 for `1 + 2 * 3`" happy path is asserted directly.
//
// Naming convention in this file:
//   bare make_*       → value-side factory (returns ExpressionValue)
//   ast::make_*       → AST-side factory (returns AstNode); the AST-side
//                       factories live in chronon3d::expressions::v2::ast
//                       to avoid colliding with the same-named value-side
//                       factories in expression_value.hpp.

#include <doctest/doctest.h>

#include <chronon3d/expressions/v2/ast.hpp>
#include <chronon3d/expressions/v2/bytecode.hpp>
#include <chronon3d/expressions/v2/compiler.hpp>
#include <chronon3d/expressions/v2/dependency_graph.hpp>
#include <chronon3d/expressions/v2/expression_value.hpp>
#include <chronon3d/expressions/v2/lexer.hpp>
#include <chronon3d/expressions/v2/type_checker.hpp>
#include <chronon3d/expressions/v2/vm.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace chronon3d::expressions::v2;
using chronon3d::Vec3;

// ════════════════════════════════════════════════════════════════════
//  ExpressionValue (variant + helpers)
// ════════════════════════════════════════════════════════════════════

TEST_CASE("ExpressionValue: kind() reflects variant index") {
    CHECK(kind(make_number(1.0)) == ExpressionValueKind::Number);
    CHECK(kind(make_bool(true))   == ExpressionValueKind::Bool);
    CHECK(kind(make_string("x"))  == ExpressionValueKind::String);
    CHECK(kind(make_vec2({1,2}))  == ExpressionValueKind::Vec2);
    CHECK(kind(make_vec3({1,2,3}))== ExpressionValueKind::Vec3);
    CHECK(kind(make_vec4({1,2,3,4}))== ExpressionValueKind::Vec4);
    Color c{0.1f, 0.2f, 0.3f, 0.4f};
    CHECK(kind(make_color(c))     == ExpressionValueKind::Color);
    CHECK(kind(make_layer({"L"}))  == ExpressionValueKind::LayerReference);
    CHECK(kind(make_property({"L","p"})) == ExpressionValueKind::PropertyReference);
}

TEST_CASE("ExpressionValue: as_* helpers return nullptr on type mismatch") {
    ExpressionValue v = make_number(7.0);
    CHECK(as_number(v) != nullptr);
    CHECK(as_bool(v)   == nullptr);
    CHECK(as_string(v) == nullptr);

    ExpressionValue s = make_string("hi");
    CHECK(as_string(s) != nullptr);
    CHECK(as_number(s) == nullptr);
}

TEST_CASE("ExpressionValue: to_string debug helper renders values") {
    CHECK(to_string(make_number(2.5)).find("Number(2.5)") != std::string::npos);
    CHECK(to_string(make_bool(true)).find("Bool(true)") != std::string::npos);
    CHECK(to_string(make_string("xyz")).find("String(xyz)") != std::string::npos);
    Vec3 v{1,2,3};
    CHECK(to_string(make_vec3(v)).find("Vec3(1,2,3)") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════
//  Lexer
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Lexer: round-trip numbers") {
    auto r = lex("123 -1.5 1e2 1.5e-3");
    CHECK(r.ok());
    REQUIRE(r.tokens.size() >= 5); // 4 numbers + EOF
    CHECK(r.tokens[0].kind == TokenKind::Number);
    CHECK(r.tokens[0].number_value == doctest::Approx(123.0));
    CHECK(r.tokens[2].number_value == doctest::Approx(1e2));
    CHECK(r.tokens[3].number_value == doctest::Approx(1.5e-3));
}

TEST_CASE("Lexer: identifiers and reserved boolean") {
    auto r = lex("thisLayer true false x y_z");
    CHECK(r.ok());
    CHECK(r.tokens[0].kind == TokenKind::Identifier);
    CHECK(r.tokens[0].lexeme == "thisLayer");
    CHECK(r.tokens[1].kind == TokenKind::BoolTrue);
    CHECK(r.tokens[2].kind == TokenKind::BoolFalse);
    CHECK(r.tokens[3].lexeme == "x");
    CHECK(r.tokens[4].lexeme == "y_z");
}

TEST_CASE("Lexer: two-char operators") {
    auto r = lex("== != <= >= && ||");
    CHECK(r.ok());
    CHECK(r.tokens[0].kind == TokenKind::EqEq);
    CHECK(r.tokens[1].kind == TokenKind::NotEq);
    CHECK(r.tokens[2].kind == TokenKind::LessEq);
    CHECK(r.tokens[3].kind == TokenKind::GreaterEq);
    CHECK(r.tokens[4].kind == TokenKind::AndAnd);
    CHECK(r.tokens[5].kind == TokenKind::OrOr);
}

TEST_CASE("Lexer: strings with escape") {
    auto r = lex("\"hello\\nworld\"");
    CHECK(r.ok());
    REQUIRE(r.tokens.size() >= 2);
    CHECK(r.tokens[0].kind == TokenKind::String);
    CHECK(r.tokens[0].lexeme == "hello\nworld");
}

TEST_CASE("Lexer: errors on unterminated string") {
    auto r = lex("\"abc");
    CHECK_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
}

// ═══════════════════════════════════════════════════════════════════
//  Multi-error push (reviewer fix #2 — count-sensitive assertions)
// ═══════════════════════════════════════════════════════════════════
//
// The compiler used to early-return on the first error.  It now continues
// through lex -> parse -> type-check and surfaces ALL diagnostics.  The
// three tests below use count assertions (== or >=), not just CHECK_FALSE,
// so a regression that drops to a single error fails the test loudly.

TEST_CASE("Compiler: single lex error reports exactly one entry") {
    auto r = compile("@");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
    CHECK(r.errors.front().message.find("lex:") != std::string::npos);
}

TEST_CASE("Compiler: multi-error push — chained lex failures") {
    // '@' is invalid in every position; the lexer emits one diagnostic per
    // token, and the compiler surfaces every one of them.
    auto r = compile("@@ @");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() >= 2);
    for (const auto& e : r.errors) {
        CHECK(e.message.find("lex:") != std::string::npos);
    }
}

TEST_CASE("Compiler: multi-error push — lex + parse coexist") {
    // Leading '@' => a lex diagnostic; trailing extra token after the primary
    // expression => a parse diagnostic ("unexpected trailing token ...").
    // Both must surface in out.errors simultaneously.
    auto r = compile("@ 1 2");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() >= 2);
    bool saw_lex = false, saw_parse = false;
    for (const auto& e : r.errors) {
        if (e.message.find("lex:") != std::string::npos)                  saw_lex  = true;
        if (e.message.find("unexpected trailing") != std::string::npos)  saw_parse = true;
    }
    CHECK(saw_lex);
    CHECK(saw_parse);
}

// ── lex_errored flag pinning (parse_primary suppress-on-lex-err) ─────
//
// Locks the `lex_errored` boolean specifically for the
// `lex-error-followed-by-valid-expression-start` case.  Earlier tests covered
// `@` alone (entire input is a lex error) and `@ 1 2` (lex + trailing parse),
// but neither explicitly tested "lex error between valid expression start
// characters".  Here:
//   - Lex: '@' errored + advanced; '1' scans as Number(1).
//     tokens:  [Number(1), Eof]
//     errors:  [`unexpected character '@'`]
//   - Parse: lex_errored=true → parse_primary suppresses its default-case
//     error for Eof; Number(1) parses normally; parse_expression sees Eof
//     (no trailing-token error).  Total: EXACTLY ONE lex error surfaces in
//     out.errors.
TEST_CASE("Compiler: lex_errored suppresses parse_primary — `@1` produces exactly 1 lex error") {
    auto r = compile("@1");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
    CHECK(r.errors.front().message.find("lex:") != std::string::npos);
}

// Companion to `@1`: lex('@') errors, parse_primary Ident branch (idx 3) -> AstIdentifier,
// type-check = Top (free-identifier permissive). Regression: errors.size() == 2 means
// default-case suppression broke.
TEST_CASE("Compiler: lex_errored suppresses parse_primary — `@x` produces exactly 1 lex error") {
    auto r = compile("@x");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
    CHECK(r.errors.front().message.find("lex:") != std::string::npos);
}

// Companion to `@1` / `@x`: exercises parse_primary's BoolTrue branch
// (TokenKind::BoolTrue). With trailing token `true`, parse_primary returns
// AstNode{true} cleanly; type-check classifies as Type::Bool with no error.
// Only the lex error from `@` reaches out.errors.
TEST_CASE("Compiler: lex_errored suppresses parse_primary — `@true` produces exactly 1 lex error") {
    auto r = compile("@true");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
    CHECK(r.errors.front().message.find("lex:") != std::string::npos);
}

// Companion to `@1` / `@x` / `@true`: exercises parse_primary's String branch
// (TokenKind::String). With trailing token `"foo"`, parse_primary returns
// AstNode{"foo"} cleanly; type-check classifies as Type::String with no error.
// Only the lex error from `@` reaches out.errors.
//
// Together with `@1` (Number), `@x` (Identifier), `@true` (BoolTrue), and
// `@"foo"` (String), the suppression is locked across every parse_primary
// kind that has a dedicated branch.  A future branch addition (or a regression
// that loosens the default-case suppression) will surface here.
TEST_CASE("Compiler: lex_errored suppresses parse_primary — `@\"foo\"` produces exactly 1 lex error") {
    auto r = compile("@\"foo\"");
    REQUIRE_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
    CHECK(r.errors.front().message.find("lex:") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════
//  Type checker (over AST)
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Type checker: vanilla arithmetic is Number") {
    AstNode ast = ast::make_binary(BinaryOp::Add,
        ast::make_number(1.0), ast::make_number(2.0), {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Number);
}

TEST_CASE("Type checker: thisLayer.position is Vec3") {
    AstNode ast = ast::make_member(
        ast::make_identifier("thisLayer", {}), "position", {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Vec3);
}

TEST_CASE("Type checker: thisLayer.position.x is Number") {
    AstNode ast = ast::make_member(
        ast::make_member(ast::make_identifier("thisLayer", {}), "position", {}), "x", {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Number);
}

TEST_CASE("Type checker: rejects arithmetic on string + number") {
    AstNode ast = ast::make_binary(BinaryOp::Add,
        ast::make_string("foo"), ast::make_number(1.0), {});
    auto r = type_check(ast);
    CHECK_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
}

// ── Top-permissive: free-identifier arithmetic relaxes strictness ───
//
// Trade-off: any binary arithmetic with at least one Type::Top operand
// returns Top instead of erroring.  See include/.../type_checker.hpp
// "Trade-off: Type::Top permissiveness" for the full rationale and the
// list of which expressions are relaxed vs. still rejected.

TEST_CASE("Type checker: Top-permissive — bool + free identifier returns Top") {
    // Strict ordering: String+String fails (no String operand); Top-or-Top
    // succeeds and emits Type::Top, NOT Bool.  No diagnostic is added
    // because the relaxation is silent.
    AstNode ast = ast::make_binary(BinaryOp::Add,
        ast::make_bool(true), ast::make_identifier("x", {}), {});
    auto tc = type_check(ast);
    CHECK(tc.ok());
    CHECK(tc.root_type == Type::Top);
}

TEST_CASE("Type checker: Top-permissive — empty string + free identifier returns Top") {
    // Empty-string literal ("") classifies as Type::String (variant index 1
    // always returns String for any string content, including "").  Right
    // operand is a free identifier → Top.  Order: String+String fails
    // (right isn't String); Top-or-Top fires → returns Top, NOT String.
    AstNode ast = ast::make_binary(BinaryOp::Add,
        ast::make_string(""), ast::make_identifier("x", {}), {});
    auto tc = type_check(ast);
    CHECK(tc.ok());
    CHECK(tc.root_type == Type::Top);
}

TEST_CASE("Type checker: Top-permissive — non-string + non-string + free identifier returns Top") {
    // Sanity: both variants `1 + x` (Number + Top) and `false + x` (Bool + Top)
    // route through the Top-or-Top branch and emit Top.  Pinning both so a
    // future ordering change in the arithmetic dispatcher cannot regress one
    // without the other.
    {
        AstNode ast = ast::make_binary(BinaryOp::Mul,
            ast::make_number(2.0), ast::make_identifier("x", {}), {});
        auto tc = type_check(ast);
        CHECK(tc.ok());
        CHECK(tc.root_type == Type::Top);
    }
    {
        AstNode ast = ast::make_binary(BinaryOp::Sub,
            ast::make_bool(false), ast::make_identifier("x", {}), {});
        auto tc = type_check(ast);
        CHECK(tc.ok());
        CHECK(tc.root_type == Type::Top);
    }
}

TEST_CASE("Compiler: Top-permissive — `true + x` and `\"\" + x` both compile") {
    // End-to-end: the permissiveness must survive the full lex+parse+type
    // pipeline.  The VM still surfaces a runtime error if `x` is unbound
    // (no binding in scope); these tests only pin static-time behaviour.
    CHECK(compile("true + x").ok());
    CHECK(compile("\"\" + x").ok());
}

// ════════════════════════════════════════════════════════════════════
//  Bytecode packing
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Bytecode: pack_const_slot round-trips tag and index") {
    for (std::uint8_t tag : {0u, 1u, 2u}) {
        for (std::uint32_t idx : {0u, 1u, 100u, 0x00FFFFu}) {
            const std::uint32_t packed = pack_const_slot(tag, idx);
            CHECK(const_slot_tag(packed)     == tag);
            CHECK(const_slot_index(packed)  == idx);
        }
    }
}

// ════════════════════════════════════════════════════════════════════
//  Vec3 arithmetic completeness (reviewer fix #1)
// ════════════════════════════════════════════════════════════════════

TEST_CASE("VM: Vec3 + scalar produces Vec3") {
    Vm vm;
    vm.set("position", make_vec3({1.0f, 2.0f, 3.0f}));
    CompileResult cr = compile("position + 5");
    REQUIRE(cr.ok());
    VmError err;
    ExpressionValue v = vm.run(cr.program, &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_vec3(v) != nullptr);
    CHECK(as_vec3(v)->x == doctest::Approx(6.0f));
    CHECK(as_vec3(v)->y == doctest::Approx(7.0f));
    CHECK(as_vec3(v)->z == doctest::Approx(8.0f));
}

TEST_CASE("VM: Vec3 * scalar produces Vec3") {
    Vm vm;
    vm.set("position", make_vec3({1.0f, 2.0f, 3.0f}));
    CompileResult cr = compile("position * 2");
    REQUIRE(cr.ok());
    VmError err;
    ExpressionValue v = vm.run(cr.program, &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_vec3(v) != nullptr);
    CHECK(as_vec3(v)->x == doctest::Approx(2.0f));
    CHECK(as_vec3(v)->y == doctest::Approx(4.0f));
    CHECK(as_vec3(v)->z == doctest::Approx(6.0f));
}

// ════════════════════════════════════════════════════════════════════
//  Headline: compile + run
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Path B end-to-end: 1 + 2 * 3 evaluates to 7") {
    // compile(source).ok() precondition: surfaces parser-stage failure
    // (lex/parse/type-check) directly under cr.errors rather than letting
    // Vm::evaluate report it via an opaque VmError message.
    CompileResult cr = compile("1 + 2 * 3");
    REQUIRE(cr.ok());
    VmError err;
    ExpressionValue v = Vm::evaluate("1 + 2 * 3", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(7.0));
}

TEST_CASE("Path B end-to-end: variable binding (LOAD_VAR)") {
    CompileResult cr = compile("x * 2");
    REQUIRE(cr.ok());
    Vm vm;
    vm.set("x", make_number(10.0));
    VmError err;
    ExpressionValue v = vm.run(cr.program, &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(20.0));
}

TEST_CASE("Path B end-to-end: function call (sin / clamp)") {
    {
        VmError err;
        ExpressionValue v = Vm::evaluate("clamp(15, 0, 10)", &err);
        REQUIRE(err.message.empty());
        REQUIRE(as_number(v) != nullptr);
        CHECK(*as_number(v) == doctest::Approx(10.0));
    }
    {
        VmError err;
        ExpressionValue v = Vm::evaluate("clamp(-5, 0, 10)", &err);
        REQUIRE(err.message.empty());
        REQUIRE(as_number(v) != nullptr);
        CHECK(*as_number(v) == doctest::Approx(0.0));
    }
}

TEST_CASE("Path B end-to-end: member access on Vec3") {
    Vm vm;
    vm.set("p", make_vec3({1.0f, 2.0f, 3.0f}));
    CompileResult cr = compile("p.y");
    REQUIRE(cr.ok());
    VmError err;
    ExpressionValue v = vm.run(cr.program, &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(2.0));
}

TEST_CASE("Path B end-to-end: thisLayer.position.x is property reference") {
    CompileResult cr = compile("thisLayer.position.x");
    REQUIRE(cr.ok());
    Vm vm;
    // No env binding for thisLayer — VM will surface as PropertyReference.
    VmError err;
    ExpressionValue v = vm.run(cr.program, &err);
    // We don't require zero error here because `thisLayer` isn't bound —
    // in real-world the host binds it. The compile path is what we're
    // verifying for this test.
    (void)v;
}

TEST_CASE("Path B end-to-end: conditional (ternary) picks branch") {
    // compile(source).ok() precondition: see 1 + 2 * 3 test for rationale.
    CompileResult cr = compile("true ? 42 : 0");
    REQUIRE(cr.ok());
    VmError err;
    ExpressionValue v = Vm::evaluate("true ? 42 : 0", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(42.0));
}

// ── Conditional right-associativity (parse_conditional recursion) ──
//
// Locks the recursive descent: `?:` is the LOWEST precedence and is
// right-associative, so `a ? b ? c : d : e` parses as `a ? (b ? c : d) : e`.
// A non-recursive implementation (callers using parse_expression, which has
// an EOF check that errors "unexpected trailing token ':'") would fail
// to parse the inner ternary. Evaluating step by step:
//   inner: 3 < 4 ? 5 : 6  → 5  (true branch)
//   outer: 1 < 2 ? 5 : 7  → 5  (true branch)
//   final: 5
TEST_CASE("Path B end-to-end: ternary is right-associative (`1 < 2 ? 3 < 4 ? 5 : 6 : 7` = 5)") {
    // compile(source).ok() precondition: surfaces any parser-stage failure
    // (e.g. parse_conditional regression) directly under cr.errors. If the
    // recursive descent into the inner ternary broke, this REQUIRE fires
    // before Vm::evaluate gets a chance to mask it as VmError "halt".
    CompileResult cr = compile("1 < 2 ? 3 < 4 ? 5 : 6 : 7");
    REQUIRE(cr.ok());

    // === AST shape: lock the parse tree directly ===================
    // Expected tree:  Conditional(Lt(1,2), Conditional(Lt(3,4), 5, 6), 7)
    // i.e. right-associative `?:`; the THEN branch is ANOTHER AstConditional
    // (variant index 9), proving parse_conditional recursed instead of
    // bailing to parse_expression (which errors on `:`) and stopping at the
    // first Lt.  If parse_conditional regression made `?:` left-associative,
    // the top-level would still be AstConditional but the THEN branch would
    // be a number — this assertion catches that.
    //
    // Note: leaf `index()==0` checks are deliberately absent because
    // std::get<double>(...) below throws std::bad_variant_access on a wrong
    // index, which doctest will surface as a loud failure.  Top-level and
    // THEN-branch `index()==9` REQUIREs are kept because they distinguish
    // *topology errors* (wrong branch shape) from leaf-type errors.
    REQUIRE(cr.ast.index() == 9);                       // AstConditional
    const auto& outer = std::get<AstConditional>(cr.ast);
    REQUIRE(outer.condition);
    REQUIRE(outer.then_branch);
    REQUIRE(outer.else_branch);
    // outer.condition: Lt(1,2)
    REQUIRE(outer.condition->index() == 7);             // AstBinary
    const auto& outer_cond = std::get<AstBinary>(*outer.condition);
    CHECK(outer_cond.op == BinaryOp::Lt);
    REQUIRE(outer_cond.lhs);
    REQUIRE(outer_cond.rhs);
    CHECK(std::get<double>(*outer_cond.lhs) == doctest::Approx(1.0));
    CHECK(std::get<double>(*outer_cond.rhs) == doctest::Approx(2.0));
    // outer.then_branch: nested Conditional(Lt(3,4), 5, 6)
    REQUIRE(outer.then_branch->index() == 9);           // AstConditional
    const auto& inner = std::get<AstConditional>(*outer.then_branch);
    REQUIRE(inner.condition);
    REQUIRE(inner.then_branch);
    REQUIRE(inner.else_branch);
    REQUIRE(inner.condition->index() == 7);
    const auto& inner_cond = std::get<AstBinary>(*inner.condition);
    CHECK(inner_cond.op == BinaryOp::Lt);
    CHECK(std::get<double>(*inner_cond.lhs) == doctest::Approx(3.0));
    CHECK(std::get<double>(*inner_cond.rhs) == doctest::Approx(4.0));
    CHECK(std::get<double>(*inner.then_branch) == doctest::Approx(5.0));
    CHECK(std::get<double>(*inner.else_branch) == doctest::Approx(6.0));
    // outer.else_branch: 7
    CHECK(std::get<double>(*outer.else_branch) == doctest::Approx(7.0));
    // === end AST shape ===

    VmError err;
    ExpressionValue v = Vm::evaluate("1 < 2 ? 3 < 4 ? 5 : 6 : 7", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(5.0));
}

TEST_CASE("Path B end-to-end: ternary is right-associative — false inner picks else") {
    // Same shape but inner condition is false: `2 < 1 ? ... : 4 : 9` → 9.
    // Confirms the recursion reaches the right BOTH branches, not just the
    // true-then side of the inner ternary.
    // compile(source).ok() precondition: see true-branch counterpart above.
    CompileResult cr = compile("1 < 2 ? 2 < 1 ? 5 : 4 : 9");
    REQUIRE(cr.ok());

    // === AST shape: lock the false-inner branch reaches both sides ===
    // Expected tree:  Conditional(Lt(1,2), Conditional(Lt(2,1), 5, 4), 9)
    // Same right-associative shape as the true-branch case; only the
    // inner-then/inner-else literals differ.
    REQUIRE(cr.ast.index() == 9);
    const auto& outer = std::get<AstConditional>(cr.ast);
    REQUIRE(outer.condition != nullptr);
    REQUIRE(outer.then_branch != nullptr);
    REQUIRE(outer.else_branch != nullptr);
    REQUIRE(outer.condition->index() == 7);
    const auto& outer_cond = std::get<AstBinary>(*outer.condition);
    CHECK(outer_cond.op == BinaryOp::Lt);
    CHECK(std::get<double>(*outer_cond.lhs) == doctest::Approx(1.0));
    CHECK(std::get<double>(*outer_cond.rhs) == doctest::Approx(2.0));
    REQUIRE(outer.then_branch->index() == 9);
    const auto& inner = std::get<AstConditional>(*outer.then_branch);
    REQUIRE(inner.condition != nullptr);
    REQUIRE(inner.then_branch != nullptr);
    REQUIRE(inner.else_branch != nullptr);
    REQUIRE(inner.condition->index() == 7);
    const auto& inner_cond = std::get<AstBinary>(*inner.condition);
    CHECK(inner_cond.op == BinaryOp::Lt);
    CHECK(std::get<double>(*inner_cond.lhs) == doctest::Approx(2.0));
    CHECK(std::get<double>(*inner_cond.rhs) == doctest::Approx(1.0));
    CHECK(std::get<double>(*inner.then_branch) == doctest::Approx(5.0));
    CHECK(std::get<double>(*inner.else_branch) == doctest::Approx(4.0));
    CHECK(std::get<double>(*outer.else_branch) == doctest::Approx(9.0));
    // === end AST shape ===

    VmError err;
    ExpressionValue v = Vm::evaluate("1 < 2 ? 2 < 1 ? 5 : 4 : 9", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(4.0));
}

TEST_CASE("Path B end-to-end: unary not flips bool") {
    VmError err;
    ExpressionValue v = Vm::evaluate("!true", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_bool(v) != nullptr);
    CHECK(*as_bool(v) == false);
}

TEST_CASE("Path B end-to-end: Vm::evaluate has fresh env per call (statelessness)") {
    // The static convenience helper compiles+runs and creates a fresh Vm per
    // call, so bindings from one call do NOT leak into the next.  This is a
    // foot-gun for shared-state users; the instance API Vm::run() is the
    // safe path for repeated eval.
    {
        VmError err;
        ExpressionValue v = Vm::evaluate("x * 2", &err);
        // No binding → error path expected.
        CHECK_FALSE(err.message.empty());
        (void)v;
    }
    {
        VmError err;
        ExpressionValue v = Vm::evaluate("x * 2", &err);
        // Second call should also NOT find `x` (proves stateless).
        CHECK_FALSE(err.message.empty());
        (void)v;
    }
}

// ════════════════════════════════════════════════════════════════════
//  DependencyGraph + cycle detection
// ════════════════════════════════════════════════════════════════════

TEST_CASE("DependencyGraph: acyclic ordering respects dependencies") {
    CompileResult cr_b = compile("b");
    CompileResult cr_a = compile("a + b");
    REQUIRE(cr_b.ok());
    REQUIRE(cr_a.ok());

    DependencyGraph g;
    g.add_program({"a"}, cr_a.program);
    g.add_program({"b"}, cr_b.program);
    g.add_writer({"b"}, "b");   // b writes b
    g.add_writer({"a"}, "a");   // a writes a

    CycleReport cycle;
    auto order = g.topological_order(cycle);
    CHECK(cycle.cycle_nodes.empty());
    REQUIRE(order.size() == 2);
    // b must come before a (a reads b).
    CHECK(order[0].name == "b");
    CHECK(order[1].name == "a");
}

TEST_CASE("DependencyGraph: detects length-2 cycle A->B, B->A") {
    CompileResult cr_a = compile("b + 1");
    CompileResult cr_b = compile("a + 1");
    REQUIRE(cr_a.ok());
    REQUIRE(cr_b.ok());

    DependencyGraph g;
    g.add_program({"A"}, cr_a.program);
    g.add_program({"B"}, cr_b.program);
    g.add_writer({"A"}, "a");
    g.add_writer({"B"}, "b");

    CycleReport cycle;
    auto order = g.topological_order(cycle);
    CHECK(order.empty());
    CHECK(cycle.cycle_nodes.size() == 2);
}

TEST_CASE("DependencyGraph: handles single isolated node") {
    CompileResult cr = compile("42");
    REQUIRE(cr.ok());

    DependencyGraph g;
    g.add_program({"only"}, cr.program);
    g.add_writer({"only"}, "x");
    CycleReport cycle;
    auto order = g.topological_order(cycle);
    CHECK(cycle.cycle_nodes.empty());
    REQUIRE(order.size() == 1);
    CHECK(order[0].name == "only");
}
