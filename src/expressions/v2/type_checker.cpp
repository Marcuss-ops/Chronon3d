// type_checker.cpp — Path B expression type checker implementation.

#include <chronon3d/expressions/v2/type_checker.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace chronon3d::expressions::v2 {

const char* type_name(Type t) noexcept {
    switch (t) {
        case Type::Unknown: return "Unknown";
        case Type::Number:  return "Number";
        case Type::String:  return "String";
        case Type::Bool:    return "Bool";
        case Type::Vec2:    return "Vec2";
        case Type::Vec3:    return "Vec3";
        case Type::Vec4:    return "Vec4";
        case Type::Color:   return "Color";
        case Type::LayerReference:    return "LayerReference";
        case Type::PropertyReference: return "PropertyReference";
        case Type::Top:     return "Top";
    }
    return "<unknown>";
}

bool is_vector_type(Type t) noexcept {
    return t == Type::Vec2 || t == Type::Vec3 || t == Type::Vec4 || t == Type::Color;
}

bool is_numeric_type(Type t) noexcept {
    return t == Type::Number;
}

// ── Widen: pick the broader of two arithmetic types ─────────────────
// Vec promotion: same dimension keep; mismatched -> Unknown;
// otherwise Top (permissive fallback used by the VM).
static Type widen(Type a, Type b) noexcept {
    if (a == b) return a;
    if (a == Type::Unknown) return b;
    if (b == Type::Unknown) return a;
    // Allow Vec+Number mixing -> Top (permissive).
    if ((is_vector_type(a) || a == Type::Number) &&
        (is_vector_type(b) || b == Type::Number)) {
        return Type::Top;
    }
    // String + String == String.
    if (a == Type::String && b == Type::String) return Type::String;
    // Bool + Bool == Bool.
    if (a == Type::Bool && b == Type::Bool) return Type::Bool;
    return Type::Unknown;
}

namespace {

// ── type_of: pull the static type out of a single AST node ───────────
struct CheckContext {
    std::vector<TypeError> errors;
};

Type type_of(const AstNode& n, CheckContext& ctx) noexcept;

Type type_of_identifier(const std::string& name, const SourceSpan& s, CheckContext& ctx) noexcept {
    if (name == "thisLayer") return Type::LayerReference;
    if (name == "thisComp")  return Type::Top;  // permissive; member access narrows
    // Anything else: starts as Top so member access can narrow.
    (void)s;
    (void)ctx;
    return Type::Top;
}

Type type_of(const AstNode& n, CheckContext& ctx) noexcept {
    switch (n.index()) {
        case 0: return Type::Number;           // double literal
        case 1: return Type::String;           // string literal (used for Identifier too, but treated as Top here)
        case 2: return Type::Bool;             // bool literal
        case 3: {                              // AstIdentifier
            const auto& id = std::get<AstIdentifier>(n);
            return type_of_identifier(id.name, id.span, ctx);
        }
        case 4: {                              // AstMemberAccess
            const auto& m = std::get<AstMemberAccess>(n);
            const Type base_t = type_of(m.base, ctx);
            if (base_t == Type::LayerReference) {
                // thisLayer.position -> Vec3  (we hardcode the common AE props)
                if (m.member == "position" || m.member == "scale" || m.member == "anchorPoint") return Type::Vec3;
                if (m.member == "rotation" || m.member == "opacity") return Type::Number;
                if (m.member == "name") return Type::String;
                return Type::Top;
            }
            if (is_vector_type(base_t)) {
                if (base_t == Type::Vec3 && (m.member == "x" || m.member == "y" || m.member == "z")) return Type::Number;
                if (base_t == Type::Vec2 && (m.member == "x" || m.member == "y")) return Type::Number;
                if (base_t == Type::Vec4 && (m.member == "x" || m.member == "y" || m.member == "z" || m.member == "w")) return Type::Number;
                if (base_t == Type::Color && (m.member == "r" || m.member == "g" || m.member == "b" || m.member == "a")) return Type::Number;
                return Type::Top;
            }
            return Type::Top;
        }
        case 5: {                              // AstIndexAccess
            const auto& idx = std::get<AstIndexAccess>(n);
            const Type base_t = type_of(idx.base, ctx);
            const Type idx_t  = type_of(idx.index, ctx);
            (void)idx_t;
            if (is_vector_type(base_t)) return Type::Number;
            return Type::Top;
        }
        case 6: {                              // AstCall
            const auto& call = std::get<AstCall>(n);
            for (const auto& a : call.args) (void)type_of(a, ctx);
            // All known built-ins are arithmetic for v2 baseline.
            const std::string& nm = call.name;
            if (nm == "sin" || nm == "cos" || nm == "tan" || nm == "sqrt" ||
                nm == "abs" || nm == "floor" || nm == "ceil" || nm == "round" ||
                nm == "min" || nm == "max" || nm == "clamp" || nm == "pow" || nm == "log") {
                return Type::Number;
            }
            // length is the only known String-input builtin → Number.
            if (nm == "length") return Type::Number;
            // Unknown function: Top (permissive).
            return Type::Top;
        }
        case 7: {                              // AstBinary
            const auto& b = std::get<AstBinary>(n);
            const Type l = type_of(b.lhs, ctx);
            const Type r = type_of(b.rhs, ctx);
            switch (b.op) {
                case BinaryOp::Add: case BinaryOp::Sub:
                case BinaryOp::Mul: case BinaryOp::Div: case BinaryOp::Mod:
                    if (l == Type::String && r == Type::String) return Type::String;
                    if ((l == Type::Number || is_vector_type(l)) &&
                        (r == Type::Number || is_vector_type(r))) return widen(l, r);
                    TypeError e;
                    e.span = b.span;
                    e.message = std::string("cannot apply arithmetic to ") + type_name(l) + " and " + type_name(r);
                    ctx.errors.push_back(std::move(e));
                    return Type::Unknown;
                case BinaryOp::Eq: case BinaryOp::Ne:
                case BinaryOp::Lt: case BinaryOp::Le:
                case BinaryOp::Gt: case BinaryOp::Ge:
                    // Allow mixed comparisons at permissive level.
                    if (l == Type::Bool && r != Type::Bool) {
                        // OK — comparing bool to number is a common AE mistake; warn but return Bool.
                        TypeError w;
                        w.span = b.span;
                        w.message = "comparing bool with non-bool value";
                        ctx.errors.push_back(std::move(w));
                    }
                    return Type::Bool;
                case BinaryOp::And: case BinaryOp::Or:
                    if (l != Type::Bool || r != Type::Bool) {
                        TypeError w;
                        w.span = b.span;
                        w.message = "logical operator expects bool operands";
                        ctx.errors.push_back(std::move(w));
                    }
                    return Type::Bool;
            }
            return Type::Unknown;
        }
        case 8: {                              // AstUnary
            const auto& u = std::get<AstUnary>(n);
            const Type operand = type_of(u.operand, ctx);
            if (u.op == UnaryOp::Neg) {
                if (operand != Type::Number && !is_vector_type(operand)) {
                    TypeError e;
                    e.span = u.span;
                    e.message = std::string("unary '-' expects Number/Vec, got ") + type_name(operand);
                    ctx.errors.push_back(std::move(e));
                    return Type::Unknown;
                }
                return operand;
            }
            // !
            if (operand != Type::Bool) {
                TypeError w;
                w.span = u.span;
                w.message = "unary '!' expects Bool";
                ctx.errors.push_back(std::move(w));
            }
            return Type::Bool;
        }
        case 9: {                              // AstConditional
            const auto& c = std::get<AstConditional>(n);
            (void)type_of(c.condition, ctx);
            const Type t_branch = type_of(c.then_branch, ctx);
            const Type e_branch = type_of(c.else_branch, ctx);
            return widen(t_branch, e_branch);
        }
    }
    return Type::Unknown;
}

} // namespace

TypeCheckResult type_check(const AstNode& root) noexcept {
    TypeCheckResult out;
    CheckContext ctx;
    out.root_type = type_of(root, ctx);
    out.errors = std::move(ctx.errors);
    return out;
}

} // namespace chronon3d::expressions::v2
