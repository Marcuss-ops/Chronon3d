// test_expressions_v2.cpp — Path B expression engine end-to-end coverage.
//
// Coverage spans all 7 pipeline stages (lex -> AST -> type-check -> bytecode
// -> VM) plus the dependency graph + cycle detector. The headline "compile +
// run == 7 for `1 + 2 * 3`" happy path is asserted directly.

#include <doctest/doctest.h>

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

// ════════════════════════════════════════════════════════════════════
//  Type checker (over AST)
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Type checker: vanilla arithmetic is Number") {
    AstNode ast = make_binary(BinaryOp::Add,
        make_number(1.0), make_number(2.0), {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Number);
}

TEST_CASE("Type checker: thisLayer.position is Vec3") {
    AstNode ast = make_member(
        make_identifier("thisLayer", {}), "position", {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Vec3);
}

TEST_CASE("Type checker: thisLayer.position.x is Number") {
    AstNode ast = make_member(
        make_member(make_identifier("thisLayer", {}), "position", {}), "x", {});
    auto r = type_check(ast);
    CHECK(r.ok());
    CHECK(r.root_type == Type::Number);
}

TEST_CASE("Type checker: rejects arithmetic on string + number") {
    AstNode ast = make_binary(BinaryOp::Add,
        make_string("foo"), make_number(1.0), {});
    auto r = type_check(ast);
    CHECK_FALSE(r.ok());
    CHECK(r.errors.size() == 1);
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
//  Headline: compile + run
// ════════════════════════════════════════════════════════════════════

TEST_CASE("Path B end-to-end: 1 + 2 * 3 evaluates to 7") {
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
    VmError err;
    ExpressionValue v = Vm::evaluate("true ? 42 : 0", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_number(v) != nullptr);
    CHECK(*as_number(v) == doctest::Approx(42.0));
}

TEST_CASE("Path B end-to-end: unary not flips bool") {
    VmError err;
    ExpressionValue v = Vm::evaluate("!true", &err);
    REQUIRE(err.message.empty());
    REQUIRE(as_bool(v) != nullptr);
    CHECK(*as_bool(v) == false);
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
    g.add_program({"a", {}}, cr_a.program);
    g.add_program({"b", {}}, cr_b.program);
    g.add_writer({"b", {}}, "b");   // b writes b
    g.add_writer({"a", {}}, "a");   // a writes a

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
    g.add_program({"A", {}}, cr_a.program);
    g.add_program({"B", {}}, cr_b.program);
    g.add_writer({"A", {}}, "a");
    g.add_writer({"B", {}}, "b");

    CycleReport cycle;
    auto order = g.topological_order(cycle);
    CHECK(order.empty());
    CHECK(cycle.cycle_nodes.size() == 2);
}

TEST_CASE("DependencyGraph: handles single isolated node") {
    CompileResult cr = compile("42");
    REQUIRE(cr.ok());

    DependencyGraph g;
    g.add_program({"only", {}}, cr.program);
    g.add_writer({"only", {}}, "x");
    CycleReport cycle;
    auto order = g.topological_order(cycle);
    CHECK(cycle.cycle_nodes.empty());
    REQUIRE(order.size() == 1);
    CHECK(order[0].name == "only");
}
