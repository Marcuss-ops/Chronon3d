// ast.hpp — Path B expression AST.
//
// Tagged union of expression nodes. SourceSpan on every node so downstream
// stages (type-checker, compiler, VM) can produce targeted diagnostics.

#pragma once

#include <chronon3d/expressions/v2/lexer.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <variant>

namespace chronon3d::expressions::v2 {

// Forward declarations so we can have variant-of-AST-nodes (mutual refs).
struct AstIdentifier;
struct AstMemberAccess;
struct AstIndexAccess;
struct AstCall;
struct AstBinary;
struct AstUnary;
struct AstConditional;

using AstNode = std::variant<
    double,                 // NumberLiteral(double)
    std::string,            // StringLiteral OR Identifier (re-use one slot)
    bool,                   // BoolLiteral
    AstIdentifier,
    AstMemberAccess,
    AstIndexAccess,
    AstCall,
    AstBinary,
    AstUnary,
    AstConditional
>;

[[nodiscard]] SourceSpan span_of(const AstNode& n);

// ── Node kinds (discriminator indices for the variant above) ─────────
// 0 double          NumberLiteral
// 1 string          Identifier  (we keep `name` in span_of / etc)
// 2 bool            BoolLiteral
// 3 AstIdentifier
// 4 AstMemberAccess
// 5 AstIndexAccess
// 6 AstCall
// 7 AstBinary
// 8 AstUnary
// 9 AstConditional

struct AstIdentifier {
    std::string name;
    SourceSpan   span{};
};

struct AstMemberAccess {
    AstNode     base;
    std::string member;
    SourceSpan  span{};
};

struct AstIndexAccess {
    AstNode  base;
    AstNode  index;
    SourceSpan span{};
};

struct AstCall {
    std::string      name;
    std::vector<AstNode> args;
    SourceSpan       span{};
};

enum class BinaryOp : std::uint8_t {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or
};

struct AstBinary {
    BinaryOp  op{BinaryOp::Add};
    AstNode   lhs;
    AstNode   rhs;
    SourceSpan span{};
};

enum class UnaryOp : std::uint8_t { Neg, Not };

struct AstUnary {
    UnaryOp   op{UnaryOp::Neg};
    AstNode   operand;
    SourceSpan span{};
};

struct AstConditional {
    AstNode  condition;
    AstNode  then_branch;
    AstNode  else_branch;
    SourceSpan span{};
};

// ── Convenience builders / accessors ─────────────────────────────────
[[nodiscard]] inline AstNode make_number(double d, SourceSpan s = {}) noexcept {
    AstNode n = d;
    (void)s;  // the variant double doesn't carry span; callers pass elsewhere
    return n;
}
[[nodiscard]] inline AstNode make_string(std::string s) {
    return AstNode{std::move(s)};
}
[[nodiscard]] inline AstNode make_bool(bool b) {
    return AstNode{b};
}
[[nodiscard]] inline AstNode make_identifier(std::string name, SourceSpan s) {
    AstIdentifier node;
    node.name = std::move(name);
    node.span = s;
    return AstNode{std::move(node)};
}
[[nodiscard]] inline AstNode make_member(AstNode base, std::string member, SourceSpan s) {
    AstMemberAccess m;
    m.base = std::move(base);
    m.member = std::move(member);
    m.span = s;
    return AstNode{std::move(m)};
}
[[nodiscard]] inline AstNode make_index(AstNode base, AstNode index, SourceSpan s) {
    AstIndexAccess node;
    node.base = std::move(base);
    node.index = std::move(index);
    node.span = s;
    return AstNode{std::move(node)};
}
[[nodiscard]] inline AstNode make_call(std::string name, std::vector<AstNode> args, SourceSpan s) {
    AstCall c;
    c.name = std::move(name);
    c.args = std::move(args);
    c.span = s;
    return AstNode{std::move(c)};
}
[[nodiscard]] inline AstNode make_binary(BinaryOp op, AstNode lhs, AstNode rhs, SourceSpan s) {
    AstBinary b;
    b.op = op;
    b.lhs = std::move(lhs);
    b.rhs = std::move(rhs);
    b.span = s;
    return AstNode{std::move(b)};
}
[[nodiscard]] inline AstNode make_unary(UnaryOp op, AstNode operand, SourceSpan s) {
    AstUnary u;
    u.op = op;
    u.operand = std::move(operand);
    u.span = s;
    return AstNode{std::move(u)};
}
[[nodiscard]] inline AstNode make_conditional(AstNode cond, AstNode t, AstNode e, SourceSpan s) {
    AstConditional node;
    node.condition = std::move(cond);
    node.then_branch = std::move(t);
    node.else_branch = std::move(e);
    node.span = s;
    return AstNode{std::move(node)};
}

} // namespace chronon3d::expressions::v2
