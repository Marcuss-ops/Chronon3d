// test_v2_headers.cpp — Direct-include canary for the v2 experimental engine.
//
// Purpose
// =======
// This test executable exists for one reason: it "#include"s every public
// header of `chronon3d_experimental::expressions::v2` from a fresh
// translation unit, with no transitive #include from any other
// `chronon3d_experimental` header. The test then performs the smallest
// possible surface check against each header so the compiler/linker will
// reject any of these conditions:
//
// PRECONDITION: This test is built ONLY when CHRONON3D_BUILD_EXPERIMENTAL=ON.
// The whole test executable (chronon3d_expressions_v2_tests) is gated by this
// flag and lives under experimental/, so when the gate is OFF (the default
// for default-configured CI presets) the entire target is absent — no
// silent skip.
//
// TODO(promotion): once Opzione B gates complete and the engine leaves
// quarantine, replace the experimental/ target_link_libraries chain with the
// public Chronon3D::SDK equivalent and migrate the include paths back to
// `<chronon3d/expressions/v2/...>`.
//
//   1. A typo or stale include path (e.g. <chrono3d/...> instead of
//      <chronon3d_experimental/...>) — caught at compile time as a
//      "file not found" error.
//   2. A header that cannot stand alone without a transitive include
//      from elsewhere in the engine — caught at compile time as an
//      "undefined type" or "incomplete type" error.
//   3. A header that breaks ABI / namespace misalignment with the path
//      it claims to expose — caught at link time.
//
// History
// =======
// Created specifically to lock the post-TICKET-003 ("<chrono3d/...>" typo
// in lexer.hpp) recovery into a hard pre-merge signal. TICKET-003 was
// latent because the existing `test_expressions_v2.cpp` was not directly
// including lexer.hpp — it reached the symbols only transitively through
// ast.hpp / compiler.hpp. A similar regression could re-degrade silently
// in the future; this canary makes such regressions loud.
//
// Scope
// =====
// Limited to public headers only:
//   expression_value.hpp
//   lexer.hpp
//   ast.hpp
//   type_checker.hpp
//   bytecode.hpp
//   compiler.hpp
//   vm.hpp
//   dependency_graph.hpp
//
// Internal headers (`op_kind_name` declared in bytecode.hpp, etc.) are
// exercised through the surface API of the public header that declares
// them. We do not redundantly exercise them — the goal is to catch
// "missing file on disk" or "wrong path" regressions, not exhaustively
// cover every type.

#include <doctest/doctest.h>

// Each include is wrapped in its own #ifdef-style guard is unnecessary:
// these headers are stable. If the include path regresses, the compiler
// will surface a "fatal error: file not found" pointing at the exact line,
// which is what we want.

// ── expression_value.hpp ─────────────────────────────────────────────
#include <chronon3d_experimental/expressions/v2/expression_value.hpp>

// ── lexer.hpp ────────────────────────────────────────────────────────
// This is the file that contained TICKET-003's typo. Post-fix, the line
// `#include <chronon3d/expressions/v2/expression_value.hpp>` inside the
// header points at `<chronon3d_experimental/expressions/v2/expression_value.hpp>`.
// We exhaust this header's public surface (LexResult / lex() / token_kind_name())
// to prove the include chain resolves end-to-end.
#include <chronon3d_experimental/expressions/v2/lexer.hpp>

// ── ast.hpp ──────────────────────────────────────────────────────────
// `ast::make_*` factories live in this header.
#include <chronon3d_experimental/expressions/v2/ast.hpp>

// ── type_checker.hpp ─────────────────────────────────────────────────
// `Type` enum + `type_check(...)` live here.
#include <chronon3d_experimental/expressions/v2/type_checker.hpp>

// ── bytecode.hpp ─────────────────────────────────────────────────────
// `OpKind`, `Program`, slot packing helpers.
#include <chronon3d_experimental/expressions/v2/bytecode.hpp>

// ── compiler.hpp ─────────────────────────────────────────────────────
// `compile(source)` and `compile_ast(ast)` entry points.
#include <chronon3d_experimental/expressions/v2/compiler.hpp>

// ── vm.hpp ───────────────────────────────────────────────────────────
// `Vm::evaluate(source)` static helper.
#include <chronon3d_experimental/expressions/v2/vm.hpp>

// ── dependency_graph.hpp ─────────────────────────────────────────────
// `DependencyGraph` + `CycleReport`.
#include <chronon3d_experimental/expressions/v2/dependency_graph.hpp>

namespace {

// ── per-header smoke checks ──────────────────────────────────────────
//
// IMPORTANT — what the quarantine DOES and DOES NOT change:
//
// The quarantine changes the INCLUDE PATH (downstream code must use
// `<chronon3d_experimental/expressions/v2/...>`). It does NOT change
// the C++ namespace the headers declare: every type still lives in
// `namespace chronon3d::expressions::v2 { ... }` exactly as before.
//
// That split is intentional: the include path difference is enough to
// keep the quarantine out of the SDK, the umbrella headers, and the
// productive render path. A wholesale namespace rename would force
// every internal reference to a lengthier qualified-id *and* require a
// V1→V2 migration at the type level, which is part of the Opzione B
// promotion criteria and out of scope for the initial quarantine.
//
// Each smoke check below references the actual C++ namespace of the
// symbol (chronon3d::expressions::v2), not the include-path prefix.
// The header below each symbol dictates the include the consumer
// must use — that is what the test exercises end-to-end.

// expression_value.hpp: as_number / make_number
constexpr bool k_ExpressionValueSmoke =
    (chronon3d::expressions::v2::make_number(0.0).index() == 0);

// lexer.hpp: lex + token_kind_name
constexpr bool k_LexerSmoke =
    (chronon3d::expressions::v2::token_kind_name(
         chronon3d::expressions::v2::TokenKind::Eof) != nullptr);

// ast.hpp: ast::make_number
constexpr bool k_AstSmoke =
    (chronon3d::expressions::v2::ast::make_number(0.0).index() == 0);

// type_checker.hpp: type_name(Type::Number)
constexpr bool k_TypeCheckerSmoke =
    (chronon3d::expressions::v2::type_name(
         chronon3d::expressions::v2::Type::Number) != nullptr);

// bytecode.hpp: pack_const_slot / const_slot_tag round-trip
constexpr bool k_BytecodeSmoke =
    (chronon3d::expressions::v2::const_slot_tag(
         chronon3d::expressions::v2::pack_const_slot(0, 0)) == 0);

// compiler.hpp: compile("1+1") returns a Program (smoke; not asserting on
// semantics — that's covered by test_expressions_v2.cpp).
inline bool k_CompilerSmoke() {
    auto r = chronon3d::expressions::v2::compile("1+1");
    return r.ok() && !r.program.empty();
}

// vm.hpp: Vm::evaluate("1+1")
inline bool k_VmSmoke() {
    chronon3d::expressions::v2::VmError err;
    auto v = chronon3d::expressions::v2::Vm::evaluate("1+1", &err);
    return err.message.empty();
}

// dependency_graph.hpp: DependencyGraph construction
constexpr bool k_DependencyGraphSmoke =
    std::is_default_constructible_v<
        chronon3d::expressions::v2::DependencyGraph>;

} // namespace

TEST_CASE("v2 headers: each public header compiles standalone (include canary)") {
    // If any of the includes above regressed to a `<chrono3d/...>` typo,
    // the executable would not even reach this TEST_CASE. The constants
    // below also cannot compile if any header declares a type that's
    // actually defined elsewhere in the engine — by referencing them at
    // test body level, we make sure the engine's public API does not
    // require anyone to include a private header to use it.
    CHECK(k_ExpressionValueSmoke);
    CHECK(k_LexerSmoke);
    CHECK(k_AstSmoke);
    CHECK(k_TypeCheckerSmoke);
    CHECK(k_BytecodeSmoke);
    CHECK(k_CompilerSmoke());
    CHECK(k_VmSmoke());
    CHECK(k_DependencyGraphSmoke);
}

TEST_CASE("v2 headers: lex() paths through the include chain (TICKET-003 canary)") {
    // The TICKET-003 regression was: `<chrono3d/expressions/v2/expression_value.hpp>`
    // inside lexer.hpp. If the include path inside lexer.hpp regresses again
    // (e.g. someone re-adds `<chrono3d/...>` instead of `<chronon3d_experimental/expressions/v2/...>`,
    // or breaks the include chain by moving expression_value.hpp without
    // updating lexer.hpp's own #include), this test fails to compile because
    // `lex()` won't link.
    auto r = chronon3d::expressions::v2::lex("1 + 2");
    REQUIRE(r.ok());
    REQUIRE(r.tokens.size() >= 4); // Number '+' Number EOF
    CHECK(r.tokens[0].kind == chronon3d::expressions::v2::TokenKind::Number);
    CHECK(r.tokens[3].kind == chronon3d::expressions::v2::TokenKind::Eof);
}
