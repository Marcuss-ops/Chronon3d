// vm.cpp — Path B VM interpreter (with packed LOAD_CONST slot design).

#include <chronon3d_experimental/expressions/v2/vm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chronon3d::expressions::v2 {

namespace {

[[nodiscard]] double num_or_nan(const ExpressionValue& v) noexcept {
    if (const auto* p = as_number(v)) return *p;
    return std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] double num_or_default(const ExpressionValue& v, double def) noexcept {
    if (const auto* p = as_number(v)) return *p;
    return def;
}

[[nodiscard]] bool truthy(const ExpressionValue& v) noexcept {
    switch (v.index()) {
        case 0: return std::get<double>(v) != 0.0;
        case 1: return std::get<bool>(v);
        case 2: return !std::get<std::string>(v).empty();
        default: return true;
    }
}

// ── Vec3 helpers for arithmetic completeness (#1 reviewer fix) ─────
// ADD/SUB/MUL/DIV/MOD all handle Vec3+Number and Vec3+Vec3 instead of
// silently coercing to NaN. Other vector dims (Vec2/Vec4/Color) still
// fall back to numeric coercion — landing those is v2.1.
[[nodiscard]] ExpressionValue vec3_op(
    OpKind k, const Vec3& a, const Vec3& b) noexcept
{
    const auto bin = [&](double av, double bv) -> double {
        switch (k) {
            case OpKind::ADD: return av + bv;
            case OpKind::SUB: return av - bv;
            case OpKind::MUL: return av * bv;
            case OpKind::DIV: return bv == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : av / bv;
            case OpKind::MOD: return bv == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : std::fmod(av, bv);
            default: return std::numeric_limits<double>::quiet_NaN();
        }
    };
    return make_vec3({
        static_cast<float>(bin(a.x, b.x)),
        static_cast<float>(bin(a.y, b.y)),
        static_cast<float>(bin(a.z, b.z)),
    });
}

[[nodiscard]] ExpressionValue vec3_op_vs_number(
    OpKind k, const Vec3& a, double b) noexcept
{
    const auto scalar_op = [&](double av) -> double {
        switch (k) {
            case OpKind::ADD: return av + b;
            case OpKind::SUB: return av - b;
            case OpKind::MUL: return av * b;
            case OpKind::DIV: return b == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : av / b;
            case OpKind::MOD: return b == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : std::fmod(av, b);
            default: return std::numeric_limits<double>::quiet_NaN();
        }
    };
    return make_vec3({
        static_cast<float>(scalar_op(a.x)),
        static_cast<float>(scalar_op(a.y)),
        static_cast<float>(scalar_op(a.z)),
    });
}

[[nodiscard]] ExpressionValue apply_binary(OpKind k, ExpressionValue lhs, ExpressionValue rhs) {
    // ── Vec arithmetic completeness: dispatch by Vec3-presence FIRST. ─
    if (const auto* a = as_vec3(lhs)) {
        if (const auto* b = as_vec3(rhs))   return vec3_op   (k, *a, *b);
        if (const auto* b = as_number(rhs)) return vec3_op_vs_number(k, *a, *b);
        // Vec3 vs other types: still NaN-coerce so the test surfaces the issue.
    }
    if (const auto* a = as_number(lhs)) {
        if (const auto* b = as_vec3(rhs))   return vec3_op_vs_number(k, *b, *a); // a + b == b + a (comm)
    }
    // ── String concat (replaces the old ADD-only string-fast-path). ──
    if (k == OpKind::ADD) {
        if (const auto* ap = as_string(lhs)) {
            if (const auto* bp = as_string(rhs)) return make_string(*ap + *bp);
            if (const auto* bp = as_number(rhs)) return make_string(*ap + std::to_string(*bp));
        }
        if (const auto* ap = as_number(lhs)) {
            if (const auto* bp = as_string(rhs)) return make_string(std::to_string(*ap) + *bp);
        }
    }
    switch (k) {
        case OpKind::ADD: return make_number(num_or_nan(lhs) + num_or_nan(rhs));
        case OpKind::SUB: return make_number(num_or_nan(lhs) - num_or_nan(rhs));
        case OpKind::MUL: return make_number(num_or_nan(lhs) * num_or_nan(rhs));
        case OpKind::DIV: {
            const double denom = num_or_nan(rhs);
            return make_number(denom == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : num_or_nan(lhs) / denom);
        }
        case OpKind::MOD: {
            const double denom = num_or_nan(rhs);
            return make_number(denom == 0.0
                ? std::numeric_limits<double>::quiet_NaN()
                : std::fmod(num_or_nan(lhs), denom));
        }
        case OpKind::EQ: return make_bool(lhs == rhs);
        case OpKind::NE: return make_bool(lhs != rhs);
        case OpKind::LT: return make_bool(num_or_nan(lhs) <  num_or_nan(rhs));
        case OpKind::LE: return make_bool(num_or_nan(lhs) <= num_or_nan(rhs));
        case OpKind::GT: return make_bool(num_or_nan(lhs) >  num_or_nan(rhs));
        case OpKind::GE: return make_bool(num_or_nan(lhs) >= num_or_nan(rhs));
        case OpKind::AND: return make_bool(truthy(lhs) && truthy(rhs));
        case OpKind::OR:  return make_bool(truthy(lhs) || truthy(rhs));
        default: return ExpressionValue{};
    }
}

[[nodiscard]] ExpressionValue apply_unary(OpKind k, ExpressionValue v) {
    switch (k) {
        case OpKind::NEG: {
            if (const auto* p = as_number(v)) return make_number(-*p);
            if (const auto* p = as_vec2(v))   return make_vec2({-p->x, -p->y});
            if (const auto* p = as_vec3(v))   return make_vec3({-p->x, -p->y, -p->z});
            if (const auto* p = as_vec4(v))   return make_vec4({-p->x, -p->y, -p->z, -p->w});
            return make_number(-num_or_nan(v));
        }
        case OpKind::NOT: return make_bool(!truthy(v));
        default: return ExpressionValue{};
    }
}

[[nodiscard]] ExpressionValue vec3_set_component(const Vec3& v, const std::string& m, double c) {
    if (m == "x") return make_vec3({static_cast<float>(c), v.y, v.z});
    if (m == "y") return make_vec3({v.x, static_cast<float>(c), v.z});
    if (m == "z") return make_vec3({v.x, v.y, static_cast<float>(c)});
    return make_number(std::numeric_limits<double>::quiet_NaN());
}

[[nodiscard]] ExpressionValue member_access(const ExpressionValue& base, const std::string& member) {
    if (const auto* p = as_vec3(base)) {
        if (member == "x") return make_number(p->x);
        if (member == "y") return make_number(p->y);
        if (member == "z") return make_number(p->z);
    }
    if (const auto* p = as_vec2(base)) {
        if (member == "x") return make_number(p->x);
        if (member == "y") return make_number(p->y);
    }
    if (const auto* p = as_vec4(base)) {
        if (member == "x") return make_number(p->x);
        if (member == "y") return make_number(p->y);
        if (member == "z") return make_number(p->z);
        if (member == "w") return make_number(p->w);
    }
    if (const auto* p = as_color(base)) {
        if (member == "r") return make_number(p->r);
        if (member == "g") return make_number(p->g);
        if (member == "b") return make_number(p->b);
        if (member == "a") return make_number(p->a);
    }
    // LayerReference.member — bind via env callback. We don't drill into
    // Scene here (v2 stays standalone). For now: produce a PropertyReference
    // so VMs at higher levels see the carry.
    if (const auto* lr = as_layer(base)) {
        PropertyReference pr;
        pr.layer_id = lr->layer_id;
        pr.property_path = member;
        return make_property(pr);
    }
    // String-typed has no member access (yet).
    return make_number(std::numeric_limits<double>::quiet_NaN());
}

[[nodiscard]] ExpressionValue index_access(const ExpressionValue& base, const ExpressionValue& index) {
    const double i = num_or_nan(index);
    const int idx = static_cast<int>(std::lround(i));
    if (const auto* p = as_vec2(base)) {
        if (idx == 0) return make_number(p->x);
        if (idx == 1) return make_number(p->y);
    }
    if (const auto* p = as_vec3(base)) {
        if (idx == 0) return make_number(p->x);
        if (idx == 1) return make_number(p->y);
        if (idx == 2) return make_number(p->z);
    }
    if (const auto* p = as_vec4(base)) {
        if (idx == 0) return make_number(p->x);
        if (idx == 1) return make_number(p->y);
        if (idx == 2) return make_number(p->z);
        if (idx == 3) return make_number(p->w);
    }
    if (const auto* p = as_color(base)) {
        if (idx == 0) return make_number(p->r);
        if (idx == 1) return make_number(p->g);
        if (idx == 2) return make_number(p->b);
        if (idx == 3) return make_number(p->a);
    }
    return make_number(std::numeric_limits<double>::quiet_NaN());
}

[[nodiscard]] ExpressionValue builtin_call(const std::string& name,
                                            std::vector<ExpressionValue> args) {
    if (name == "sin")  return make_number(std::sin (num_or_nan(args.at(0))));
    if (name == "cos")  return make_number(std::cos (num_or_nan(args.at(0))));
    if (name == "tan")  return make_number(std::tan (num_or_nan(args.at(0))));
    if (name == "sqrt") return make_number(std::sqrt(num_or_nan(args.at(0))));
    if (name == "abs")  return make_number(std::fabs(num_or_nan(args.at(0))));
    if (name == "floor")return make_number(std::floor(num_or_nan(args.at(0))));
    if (name == "ceil") return make_number(std::ceil(num_or_nan(args.at(0))));
    if (name == "round")return make_number(std::round(num_or_nan(args.at(0))));
    if (name == "pow")  return make_number(std::pow(num_or_nan(args.at(0)), num_or_nan(args.at(1))));
    if (name == "log")  return make_number(std::log (num_or_nan(args.at(0))));
    if (name == "min")  {
        double m = num_or_nan(args.at(0));
        for (std::size_t i = 1; i < args.size(); ++i) m = std::min(m, num_or_nan(args[i]));
        return make_number(m);
    }
    if (name == "max")  {
        double m = num_or_nan(args.at(0));
        for (std::size_t i = 1; i < args.size(); ++i) m = std::max(m, num_or_nan(args[i]));
        return make_number(m);
    }
    if (name == "clamp") {
        const double x = num_or_nan(args.at(0));
        const double lo = num_or_nan(args.at(1));
        const double hi = num_or_nan(args.at(2));
        return make_number(std::clamp(x, lo, hi));
    }
    if (name == "length") {
        if (const auto* s = as_string(args.at(0))) return make_number(static_cast<double>(s->size()));
    }
    return make_number(std::numeric_limits<double>::quiet_NaN());
}

} // namespace

ExpressionValue Vm::run(const Program& program, VmError* err) {
    std::vector<ExpressionValue> stack;
    stack.reserve(16);

    std::uint32_t pc = 0;
    while (pc < program.code.size()) {
        const Op& op = program.code[pc];
        switch (op.kind) {
            case OpKind::LOAD_CONST: {
                const std::uint8_t tag = const_slot_tag(op.slot);
                const std::uint32_t idx = const_slot_index(op.slot);
                if (tag == 0) {
                    if (idx >= program.const_numbers.size()) {
                        if (err) { err->pc = pc; err->message = "LOAD_CONST number idx out of range"; }
                        return ExpressionValue{};
                    }
                    stack.push_back(make_number(program.const_numbers[idx]));
                } else if (tag == 1) {
                    if (idx >= program.const_strings.size()) {
                        if (err) { err->pc = pc; err->message = "LOAD_CONST string idx out of range"; }
                        return ExpressionValue{};
                    }
                    stack.push_back(make_string(program.const_strings[idx]));
                } else if (tag == 2) {
                    if (idx >= program.const_bools.size()) {
                        if (err) { err->pc = pc; err->message = "LOAD_CONST bool idx out of range"; }
                        return ExpressionValue{};
                    }
                    stack.push_back(make_bool(program.const_bools[idx]));
                } else {
                    if (err) { err->pc = pc; err->message = "LOAD_CONST unknown tag"; }
                    return ExpressionValue{};
                }
                break;
            }
            case OpKind::LOAD_VAR: {
                if (op.slot >= program.names.size()) {
                    if (err) { err->pc = pc; err->message = "LOAD_VAR name idx out of range"; }
                    return ExpressionValue{};
                }
                const std::string& nm = program.names[op.slot];
                auto it = env_.find(nm);
                if (it == env_.end()) {
                    if (err) { err->pc = pc; err->message = "unknown variable: " + nm; }
                    return ExpressionValue{};
                }
                stack.push_back(it->second);
                break;
            }
            case OpKind::STORE_VAR: {
                if (op.slot >= program.names.size()) {
                    if (err) { err->pc = pc; err->message = "STORE_VAR name idx out of range"; }
                    return ExpressionValue{};
                }
                if (stack.empty()) {
                    if (err) { err->pc = pc; err->message = "STORE_VAR with empty stack"; }
                    return ExpressionValue{};
                }
                env_[program.names[op.slot]] = std::move(stack.back());
                stack.pop_back();
                break;
            }
            case OpKind::ADD: case OpKind::SUB: case OpKind::MUL: case OpKind::DIV:
            case OpKind::MOD: case OpKind::EQ: case OpKind::NE: case OpKind::LT:
            case OpKind::LE: case OpKind::GT: case OpKind::GE: case OpKind::AND:
            case OpKind::OR: {
                if (stack.size() < 2) {
                    if (err) { err->pc = pc; err->message = "binary op with <2 stack items"; }
                    return ExpressionValue{};
                }
                ExpressionValue rhs = std::move(stack.back()); stack.pop_back();
                ExpressionValue lhs = std::move(stack.back()); stack.pop_back();
                stack.push_back(apply_binary(op.kind, std::move(lhs), std::move(rhs)));
                break;
            }
            case OpKind::NEG: case OpKind::NOT: {
                if (stack.empty()) {
                    if (err) { err->pc = pc; err->message = "unary op with empty stack"; }
                    return ExpressionValue{};
                }
                ExpressionValue v = std::move(stack.back()); stack.pop_back();
                stack.push_back(apply_unary(op.kind, std::move(v)));
                break;
            }
            case OpKind::MEMBER: {
                if (stack.empty()) {
                    if (err) { err->pc = pc; err->message = "MEMBER with empty stack"; }
                    return ExpressionValue{};
                }
                if (op.slot >= program.names.size()) {
                    if (err) { err->pc = pc; err->message = "MEMBER name idx out of range"; }
                    return ExpressionValue{};
                }
                ExpressionValue base = std::move(stack.back()); stack.pop_back();
                stack.push_back(member_access(base, program.names[op.slot]));
                break;
            }
            case OpKind::INDEX: {
                if (stack.size() < 2) {
                    if (err) { err->pc = pc; err->message = "INDEX with <2 stack items"; }
                    return ExpressionValue{};
                }
                ExpressionValue i = std::move(stack.back()); stack.pop_back();
                ExpressionValue b = std::move(stack.back()); stack.pop_back();
                stack.push_back(index_access(b, i));
                break;
            }
            case OpKind::CALL: {
                if (op.slot >= program.names.size()) {
                    if (err) { err->pc = pc; err->message = "CALL name idx out of range"; }
                    return ExpressionValue{};
                }
                const std::string& fn = program.names[op.slot];
                int arity = 1;
                if (fn == "pow" || fn == "min" || fn == "max") arity = 2;
                if (fn == "clamp") arity = 3;
                if (static_cast<int>(stack.size()) < arity) {
                    if (err) { err->pc = pc; err->message = "CALL " + fn + " missing args"; }
                    return ExpressionValue{};
                }
                std::vector<ExpressionValue> args;
                args.reserve(arity);
                for (int i = arity - 1; i >= 0; --i) {
                    args.insert(args.begin(), std::move(stack[stack.size() - arity + i]));
                }
                for (int i = 0; i < arity; ++i) stack.pop_back();
                stack.push_back(builtin_call(fn, std::move(args)));
                break;
            }
            case OpKind::JMP:            pc = op.slot; continue;
            case OpKind::JMP_IF_FALSE: {
                if (stack.empty()) {
                    if (err) { err->pc = pc; err->message = "JMP_IF_FALSE with empty stack"; }
                    return ExpressionValue{};
                }
                ExpressionValue cond = std::move(stack.back()); stack.pop_back();
                if (!truthy(cond)) { pc = op.slot; continue; }
                break;
            }
            case OpKind::JMP_IF_TRUE: {
                if (stack.empty()) {
                    if (err) { err->pc = pc; err->message = "JMP_IF_TRUE with empty stack"; }
                    return ExpressionValue{};
                }
                ExpressionValue cond = std::move(stack.back()); stack.pop_back();
                if (truthy(cond)) { pc = op.slot; continue; }
                break;
            }
            case OpKind::RETURN:
                if (stack.empty()) return ExpressionValue{};
                return std::move(stack.back());
        }
        ++pc;
    }
    return ExpressionValue{};
}

ExpressionValue Vm::evaluate(std::string_view source, VmError* err) {
    CompileResult cr = compile(source);
    if (!cr.ok()) {
        if (err) { err->pc = 0; err->message = "compile failed"; }
        return ExpressionValue{};
    }
    Vm vm;
    return vm.run(cr.program, err);
}

} // namespace chronon3d::expressions::v2
