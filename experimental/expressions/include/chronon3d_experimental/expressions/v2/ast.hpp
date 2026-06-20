// ast.hpp — Path B expression AST.
//
// Tagged union of expression nodes. SourceSpan on every node so downstream
// stages (type-checker, compiler, VM) can produce targeted diagnostics.
//
// === Cycle resolution: inheritance pattern ===
//
// std::variant requires every alternative to be a complete type at the
// variant's syntactic position, AND a forward-declared `struct AstNode`
// cannot later be redefined as `using AstNode = std::variant<...>`
// (illegal: a struct declaration cannot be fulfilled by a type alias).
//
// The canonical fix: declare `struct AstNode;` as an opaque struct,
// cross-referencing sibling structs use `std::unique_ptr<AstNode>`
// (which only needs the forward decl), and the full `AstNode` type
// definition inherits from `std::variant<...>` AFTER all alterna-
// tive structs are complete.  Inheriting from std::variant gives
// AstNode all variant methods (index(), std::get<>, std::visit<>)
// without the aliasing conflict.
//
// `using std::variant::variant;` in AstNode pulls in all variant
// constructors, so callers can still write `AstNode{42.0}` or
// `AstNode{std::move(s)}` exactly as if AstNode were a variant.
//

#pragma once

#include <chronon3d_experimental/expressions/v2/lexer.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chronon3d::expressions::v2 {

// ── Forward declaration of AstNode (opaque struct so unique_ptr<AstNode>
//    members in cross-referencing structs are well-formed) ────────────
struct AstNode;

// ── Full struct definitions ─────────────────────────────────────────
//
// Sibling structs (AstMemberAccess, AstIndexAccess, AstCall, AstBinary,
// AstUnary, AstConditional) hold children via `std::unique_ptr<AstNode>`.
// unique_ptr does NOT need AstNode to be complete here; the AstNode forward
// declaration above is sufficient.  Each struct IS complete on its own
// because no value-member refers to AstNode.
//
// AstIdentifier holds a std::string directly (variable-name) and a
// SourceSpan — no child nodes, so no unique_ptr.

struct AstIdentifier {
    std::string name;
    SourceSpan   span{};
};
struct AstMemberAccess {
    std::unique_ptr<AstNode> base;
    std::string              member;
    SourceSpan               span{};
};
struct AstIndexAccess {
    std::unique_ptr<AstNode> base;
    std::unique_ptr<AstNode> index;
    SourceSpan               span{};
};
struct AstCall {
    std::string                          name;
    std::vector<std::unique_ptr<AstNode>> args;
    SourceSpan                           span{};
};
enum class BinaryOp : std::uint8_t {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or
};
struct AstBinary {
    BinaryOp                  op{BinaryOp::Add};
    std::unique_ptr<AstNode>  lhs;
    std::unique_ptr<AstNode>  rhs;
    SourceSpan                span{};
};
enum class UnaryOp : std::uint8_t { Neg, Not };
struct AstUnary {
    UnaryOp                   op{UnaryOp::Neg};
    std::unique_ptr<AstNode>  operand;
    SourceSpan                span{};
};
struct AstConditional {
    std::unique_ptr<AstNode>  condition;
    std::unique_ptr<AstNode>  then_branch;
    std::unique_ptr<AstNode>  else_branch;
    SourceSpan                span{};
};

// ── AstNode (full definition, AFTER all alternative structs are
//    complete) — inherits from std::variant so it acts as a variant
//    but is itself a class type that can be forward-declared.
//
// Node kinds by alternative index (inherited from std::variant):
//   0 double                       NumberLiteral
//   1 std::string                  StringLiteral / Identifier-by-string
//   2 bool                         BoolLiteral
//   3 AstIdentifier
//   4 AstMemberAccess              (owns base via unique_ptr<AstNode>)
//   5 AstIndexAccess               (owns base+index via unique_ptr<AstNode>)
//   6 AstCall                      (owns args[] via unique_ptr<AstNode>)
//   7 AstBinary                    (owns lhs+rhs via unique_ptr<AstNode>)
//   8 AstUnary                     (owns operand via unique_ptr<AstNode>)
//   9 AstConditional               (owns condition/then/else via unique_ptr<AstNode>)
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

// Lifecycle invariants for AstNode:
//   - Never deleted via std::variant* / AstNode* (std::variant has no virtual
//     destructor, so polymorphic delete would be UB).
//   - Stored by value, moved, or owned via std::unique_ptr<AstNode>.
//   - Currently no virtual functions on AstNode (kept non-polymorphic so
//     std::is_polymorphic_v<AstNode> stays false — verified by static_assert
//     below, evaluated once AstNode is a complete type).
static_assert(!std::is_polymorphic_v<AstNode>,
              "AstNode must stay non-polymorphic: std::variant has no virtual dtor");
static_assert(!std::has_virtual_destructor_v<AstNode>,
              "AstNode must not gain a virtual destructor");

// ── AST factory sub-namespace ───────────────────────────────────────
//
// `chronon3d::expressions::v2::ast::make_*()` — unambiguous because
// they live in a different sub-namespace from the value-side factories
// in expression_value.hpp.  Each call returns an AstNode by value; child
// nodes that need cross-reference storage use std::make_unique<AstNode>.
namespace ast {

[[nodiscard]] inline AstNode make_number(double d, SourceSpan /*s*/ = {}) noexcept {
    return AstNode{d};
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
    m.base = std::make_unique<AstNode>(std::move(base));
    m.member = std::move(member);
    m.span = s;
    return AstNode{std::move(m)};
}

[[nodiscard]] inline AstNode make_index(AstNode base, AstNode index, SourceSpan s) {
    AstIndexAccess node;
    node.base  = std::make_unique<AstNode>(std::move(base));
    node.index = std::make_unique<AstNode>(std::move(index));
    node.span = s;
    return AstNode{std::move(node)};
}

[[nodiscard]] inline AstNode make_call(std::string name, std::vector<AstNode> args, SourceSpan s) {
    AstCall c;
    c.name = std::move(name);
    c.args.reserve(args.size());
    for (auto& a : args) c.args.push_back(std::make_unique<AstNode>(std::move(a)));
    c.span = s;
    return AstNode{std::move(c)};
}

[[nodiscard]] inline AstNode make_binary(BinaryOp op, AstNode lhs, AstNode rhs, SourceSpan s) {
    AstBinary b;
    b.op = op;
    b.lhs = std::make_unique<AstNode>(std::move(lhs));
    b.rhs = std::make_unique<AstNode>(std::move(rhs));
    b.span = s;
    return AstNode{std::move(b)};
}

[[nodiscard]] inline AstNode make_unary(UnaryOp op, AstNode operand, SourceSpan s) {
    AstUnary u;
    u.op = op;
    u.operand = std::make_unique<AstNode>(std::move(operand));
    u.span = s;
    return AstNode{std::move(u)};
}

[[nodiscard]] inline AstNode make_conditional(AstNode cond, AstNode t, AstNode e, SourceSpan s) {
    AstConditional node;
    node.condition   = std::make_unique<AstNode>(std::move(cond));
    node.then_branch = std::make_unique<AstNode>(std::move(t));
    node.else_branch = std::make_unique<AstNode>(std::move(e));
    node.span = s;
    return AstNode{std::move(node)};
}

} // namespace ast

} // namespace chronon3d::expressions::v2
