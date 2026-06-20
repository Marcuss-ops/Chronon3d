// compiler.cpp — Path B compiler with the new bytecode design
//   (LOAD_CONST slot packs type tag in high 8 bits + index in low 24 bits;
//    Program.names holds all interned identifiers / function names).

#include <chronon3d/expressions/v2/compiler.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace chronon3d::expressions::v2 {

namespace {

// ── Emitter ──────────────────────────────────────────────────────────
struct Emitter {
    Program p;

    void emit(OpKind k, std::uint32_t slot = 0) {
        Op o;
        o.kind = k;
        o.slot = slot;
        p.code.push_back(o);
    }

    std::uint32_t intern_name(const std::string& name) {
        for (std::uint32_t i = 0; i < p.names.size(); ++i) {
            if (p.names[i] == name) return i;
        }
        p.names.push_back(name);
        return static_cast<std::uint32_t>(p.names.size()) - 1;
    }

    std::uint32_t emit_load_const_number(double v) {
        p.const_numbers.push_back(v);
        const std::uint32_t idx = static_cast<std::uint32_t>(p.const_numbers.size()) - 1;
        return pack_const_slot(0, idx);
    }
    std::uint32_t emit_load_const_string(const std::string& s) {
        p.const_strings.push_back(s);
        const std::uint32_t idx = static_cast<std::uint32_t>(p.const_strings.size()) - 1;
        return pack_const_slot(1, idx);
    }
    std::uint32_t emit_load_const_bool(bool b) {
        p.const_bools.push_back(b);
        const std::uint32_t idx = static_cast<std::uint32_t>(p.const_bools.size()) - 1;
        return pack_const_slot(2, idx);
    }

    void patch_jump_target(std::uint32_t instr_index, std::uint32_t target) {
        if (instr_index < p.code.size()) {
            p.code[instr_index].slot = target;
        }
    }
};

// ── Parser (recursive descent; small precedence table) ──────────────
// Grammar (informal):
//   expr            := conditional
//   conditional     := or ('?' expr ':' expr)?
//   or              := and ('||' and)*
//   and             := equality ('&&' equality)*
//   equality        := comparison (('=='|'!=') comparison)?
//   comparison      := add (('<'|'<='|'>'|'>=') add)*
//   add             := mul (('+'|'-') mul)*
//   mul             := unary (('*'|'/'|'%') unary)*
//   unary           := ('-'|'!')? postfix
//   postfix         := primary ('.' IDENT | '[' expr ']' | '(' arglist ')')*

struct Parser {
    const std::vector<Token>& tokens;
    std::size_t               pos{0};
    std::vector<CompileError> errors;

    explicit Parser(const std::vector<Token>& toks) : tokens(toks) {}

    [[nodiscard]] const Token& peek(std::size_t off = 0) const noexcept {
        return (pos + off < tokens.size()) ? tokens[pos + off] : tokens.back();
    }
    [[nodiscard]] const Token& current() const noexcept { return peek(0); }
    [[nodiscard]] bool eof() const noexcept { return current().kind == TokenKind::Eof; }

    bool consume(TokenKind k, std::string message_on_fail = "") {
        if (current().kind == k) { ++pos; return true; }
        if (!message_on_fail.empty()) {
            CompileError e;
            e.span = current().span;
            e.message = std::move(message_on_fail) + " (got '" + token_kind_name(current().kind) + "')";
            errors.push_back(std::move(e));
        }
        return false;
    }

    bool match(TokenKind k) noexcept {
        if (current().kind == k) { ++pos; return true; }
        return false;
    }

    void error_here(const std::string& msg) {
        CompileError e;
        e.span = current().span;
        e.message = msg;
        errors.push_back(std::move(e));
    }

    BinaryOp bin_op_for(TokenKind k) const noexcept {
        switch (k) {
            case TokenKind::Plus:        return BinaryOp::Add;
            case TokenKind::Minus:       return BinaryOp::Sub;
            case TokenKind::Star:        return BinaryOp::Mul;
            case TokenKind::Slash:       return BinaryOp::Div;
            case TokenKind::Percent:     return BinaryOp::Mod;
            case TokenKind::EqEq:        return BinaryOp::Eq;
            case TokenKind::NotEq:       return BinaryOp::Ne;
            case TokenKind::Less:        return BinaryOp::Lt;
            case TokenKind::LessEq:      return BinaryOp::Le;
            case TokenKind::Greater:     return BinaryOp::Gt;
            case TokenKind::GreaterEq:   return BinaryOp::Ge;
            case TokenKind::AndAnd:      return BinaryOp::And;
            case TokenKind::OrOr:        return BinaryOp::Or;
            default: return BinaryOp::Add;
        }
    }

    AstNode parse_expression() {
        AstNode node = parse_conditional();
        if (current().kind != TokenKind::Eof) {
            error_here(std::string("unexpected trailing token '") + token_kind_name(current().kind) + "'");
            while (!eof()) ++pos;
        }
        return node;
    }

    AstNode parse_conditional() {
        AstNode cond = parse_or();
        if (match(TokenKind::Question)) {
            SourceSpan s = current().span;
            AstNode t_branch = parse_expression();
            if (!consume(TokenKind::Colon, "expected ':' for conditional")) {
                return make_conditional(std::move(cond), std::move(t_branch),
                                        make_bool(false), s);
            }
            AstNode e_branch = parse_expression();
            return make_conditional(std::move(cond), std::move(t_branch), std::move(e_branch), s);
        }
        return cond;
    }

    AstNode parse_or() {
        AstNode left = parse_and();
        while (current().kind == TokenKind::OrOr) {
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_and();
            left = make_binary(BinaryOp::Or, std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_and() {
        AstNode left = parse_equality();
        while (current().kind == TokenKind::AndAnd) {
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_equality();
            left = make_binary(BinaryOp::And, std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_equality() {
        AstNode left = parse_comparison();
        while (current().kind == TokenKind::EqEq || current().kind == TokenKind::NotEq) {
            const TokenKind k = current().kind;
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_comparison();
            left = make_binary(bin_op_for(k), std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_comparison() {
        AstNode left = parse_add();
        while (current().kind == TokenKind::Less  || current().kind == TokenKind::LessEq ||
               current().kind == TokenKind::Greater || current().kind == TokenKind::GreaterEq) {
            const TokenKind k = current().kind;
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_add();
            left = make_binary(bin_op_for(k), std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_add() {
        AstNode left = parse_mul();
        while (current().kind == TokenKind::Plus || current().kind == TokenKind::Minus) {
            const TokenKind k = current().kind;
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_mul();
            left = make_binary(bin_op_for(k), std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_mul() {
        AstNode left = parse_unary();
        while (current().kind == TokenKind::Star || current().kind == TokenKind::Slash ||
               current().kind == TokenKind::Percent) {
            const TokenKind k = current().kind;
            const SourceSpan s = current().span;
            ++pos;
            AstNode right = parse_unary();
            left = make_binary(bin_op_for(k), std::move(left), std::move(right), s);
        }
        return left;
    }

    AstNode parse_unary() {
        if (current().kind == TokenKind::Minus) {
            const SourceSpan s = current().span;
            ++pos;
            AstNode operand = parse_unary();
            return make_unary(UnaryOp::Neg, std::move(operand), s);
        }
        if (current().kind == TokenKind::Bang) {
            const SourceSpan s = current().span;
            ++pos;
            AstNode operand = parse_unary();
            return make_unary(UnaryOp::Not, std::move(operand), s);
        }
        return parse_postfix();
    }

    AstNode parse_postfix() {
        AstNode node = parse_primary();
        while (true) {
            if (current().kind == TokenKind::Dot) {
                ++pos;
                if (current().kind != TokenKind::Identifier) {
                    error_here("expected identifier after '.'");
                    break;
                }
                std::string member = current().lexeme;
                const SourceSpan ms = current().span;
                ++pos;
                node = make_member(std::move(node), std::move(member), ms);
            } else if (current().kind == TokenKind::LBracket) {
                ++pos;
                AstNode idx = parse_conditional();
                consume(TokenKind::RBracket, "expected ']' after index");
                node = make_index(std::move(node), std::move(idx), current().span);
            } else if (current().kind == TokenKind::LParen) {
                // Treat as a call: node must be Identifier-as-string.
                std::string fn_name;
                SourceSpan cs = current().span;
                if (const auto* sptr = std::get_if<std::string>(&node)) {
                    fn_name = *sptr;
                    node = std::string{};
                } else {
                    error_here("call target must be an identifier");
                    break;
                }
                ++pos;
                std::vector<AstNode> args;
                if (current().kind != TokenKind::RParen) {
                    args.push_back(parse_conditional());
                    while (match(TokenKind::Comma)) {
                        args.push_back(parse_conditional());
                    }
                }
                consume(TokenKind::RParen, "expected ')' to close call");
                node = make_call(std::move(fn_name), std::move(args), cs);
            } else {
                break;
            }
        }
        return node;
    }

    AstNode parse_primary() {
        const Token& tok = current();
        switch (tok.kind) {
            case TokenKind::Number: {
                AstNode n = make_number(tok.number_value);
                ++pos;
                return n;
            }
            case TokenKind::String: {
                AstNode n = make_string(tok.lexeme);
                ++pos;
                return n;
            }
            case TokenKind::BoolTrue: {
                AstNode n = make_bool(true);
                ++pos;
                return n;
            }
            case TokenKind::BoolFalse: {
                AstNode n = make_bool(false);
                ++pos;
                return n;
            }
            case TokenKind::Identifier: {
                AstNode n = make_identifier(tok.lexeme, tok.span);
                ++pos;
                return n;
            }
            case TokenKind::LParen: {
                ++pos;
                AstNode inner = parse_conditional();
                consume(TokenKind::RParen, "expected ')' to close group");
                return inner;
            }
            default:
                error_here(std::string("unexpected '") + token_kind_name(tok.kind) + "' at start of expression");
                AstNode placeholder = make_number(0.0);
                ++pos;
                return placeholder;
        }
    }
};

// ── Bytecode emitter ────────────────────────────────────────────────
void emit_ast(const AstNode& ast, Emitter& e);

void emit_ast(const AstNode& ast, Emitter& e) {
    switch (ast.index()) {
        case 0:
            e.emit(OpKind::LOAD_CONST, e.emit_load_const_number(std::get<double>(ast)));
            break;
        case 1:
            e.emit(OpKind::LOAD_CONST, e.emit_load_const_string(std::get<std::string>(ast)));
            break;
        case 2:
            e.emit(OpKind::LOAD_CONST, e.emit_load_const_bool(std::get<bool>(ast)));
            break;
        case 3: {
            const auto& id = std::get<AstIdentifier>(ast);
            e.emit(OpKind::LOAD_VAR, e.intern_name(id.name));
            break;
        }
        case 4: {
            const auto& m = std::get<AstMemberAccess>(ast);
            emit_ast(m.base, e);
            e.emit(OpKind::MEMBER, e.intern_name(m.member));
            break;
        }
        case 5: {
            const auto& i = std::get<AstIndexAccess>(ast);
            emit_ast(i.base, e);
            emit_ast(i.index, e);
            e.emit(OpKind::INDEX, 0);
            break;
        }
        case 6: {
            const auto& call = std::get<AstCall>(ast);
            for (const auto& a : call.args) emit_ast(a, e);
            e.emit(OpKind::CALL, e.intern_name(call.name));
            break;
        }
        case 7: {
            const auto& b = std::get<AstBinary>(ast);
            emit_ast(b.lhs, e);
            emit_ast(b.rhs, e);
            OpKind k = OpKind::ADD;
            switch (b.op) {
                case BinaryOp::Add: k = OpKind::ADD; break;
                case BinaryOp::Sub: k = OpKind::SUB; break;
                case BinaryOp::Mul: k = OpKind::MUL; break;
                case BinaryOp::Div: k = OpKind::DIV; break;
                case BinaryOp::Mod: k = OpKind::MOD; break;
                case BinaryOp::Eq:  k = OpKind::EQ;  break;
                case BinaryOp::Ne:  k = OpKind::NE;  break;
                case BinaryOp::Lt:  k = OpKind::LT;  break;
                case BinaryOp::Le:  k = OpKind::LE;  break;
                case BinaryOp::Gt:  k = OpKind::GT;  break;
                case BinaryOp::Ge:  k = OpKind::GE;  break;
                case BinaryOp::And: k = OpKind::AND; break;
                case BinaryOp::Or:  k = OpKind::OR;  break;
            }
            e.emit(k, 0);
            break;
        }
        case 8: {
            const auto& u = std::get<AstUnary>(ast);
            emit_ast(u.operand, e);
            e.emit(u.op == UnaryOp::Neg ? OpKind::NEG : OpKind::NOT, 0);
            break;
        }
        case 9: {
            const auto& c = std::get<AstConditional>(ast);
            emit_ast(c.condition, e);
            std::uint32_t jf_instr = static_cast<std::uint32_t>(e.p.code.size());
            e.emit(OpKind::JMP_IF_FALSE, 0xFFFFFFFE);
            emit_ast(c.then_branch, e);
            std::uint32_t jmp_instr = static_cast<std::uint32_t>(e.p.code.size());
            e.emit(OpKind::JMP, 0xFFFFFFFE);
            e.patch_jump_target(jf_instr, static_cast<std::uint32_t>(e.p.code.size()));
            emit_ast(c.else_branch, e);
            e.patch_jump_target(jmp_instr, static_cast<std::uint32_t>(e.p.code.size()));
            break;
        }
    }
}

} // namespace

CompileResult compile(std::string_view source) noexcept {
    CompileResult out;
    LexResult lexed = lex(source);
    for (const auto& le : lexed.errors) {
        CompileError ce;
        ce.span = le.span;
        ce.message = std::string("lex: ") + le.message;
        out.errors.push_back(std::move(ce));
    }
    Parser p(lexed.tokens);
    AstNode ast = p.parse_expression();
    for (auto& e : p.errors) out.errors.push_back(std::move(e));
    if (!out.errors.empty()) return out;

    TypeCheckResult tc = type_check(ast);
    for (auto& te : tc.errors) {
        CompileError ce;
        ce.span = te.span;
        ce.message = std::string("type: ") + te.message;
        out.errors.push_back(std::move(ce));
        // Surface ALL diagnostics: do not early-return on the first error
        // (reviewer fix #2 — type-error multi-push).
    }
    if (!out.errors.empty()) return out;

    Emitter emitter;
    emit_ast(ast, emitter);
    emitter.emit(OpKind::RETURN, 0);
    out.program = std::move(emitter.p);
    return out;
}

CompileResult compile_ast(const AstNode& ast) noexcept {
    CompileResult out;
    TypeCheckResult tc = type_check(ast);
    for (auto& te : tc.errors) {
        CompileError ce;
        ce.span = te.span;
        ce.message = std::string("type: ") + te.message;
        out.errors.push_back(std::move(ce));
    }
    Emitter emitter;
    emit_ast(ast, emitter);
    emitter.emit(OpKind::RETURN, 0);
    out.program = std::move(emitter.p);
    return out;
}

} // namespace chronon3d::expressions::v2
