// lexer.cpp — Path B expression lexer implementation.

#include <chronon3d/expressions/v2/lexer.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::expressions::v2 {

const char* token_kind_name(TokenKind k) noexcept {
    switch (k) {
        case TokenKind::Number:    return "Number";
        case TokenKind::String:    return "String";
        case TokenKind::Identifier:return "Identifier";
        case TokenKind::BoolTrue:  return "true";
        case TokenKind::BoolFalse: return "false";
        case TokenKind::LParen:    return "(";
        case TokenKind::RParen:    return ")";
        case TokenKind::LBracket:  return "[";
        case TokenKind::RBracket:  return "]";
        case TokenKind::Comma:     return ",";
        case TokenKind::Dot:       return ".";
        case TokenKind::Arrow:     return "->";
        case TokenKind::Plus:      return "+";
        case TokenKind::Minus:     return "-";
        case TokenKind::Star:      return "*";
        case TokenKind::Slash:     return "/";
        case TokenKind::Percent:   return "%";
        case TokenKind::EqEq:      return "==";
        case TokenKind::NotEq:     return "!=";
        case TokenKind::Less:      return "<";
        case TokenKind::LessEq:    return "<=";
        case TokenKind::Greater:   return ">";
        case TokenKind::GreaterEq: return ">=";
        case TokenKind::AndAnd:    return "&&";
        case TokenKind::OrOr:      return "||";
        case TokenKind::Bang:      return "!";
        case TokenKind::Question:  return "?";
        case TokenKind::Colon:     return ":";
        case TokenKind::Eof:       return "<eof>";
    }
    return "<unknown>";
}

std::string to_string(TokenKind k) {
    return std::string(token_kind_name(k));
}

namespace {

struct Scanner {
    std::string_view src;
    std::uint32_t   pos{0};
    std::uint32_t   line{1};
    std::uint32_t   col{1};
    std::vector<Token> tokens;
    std::vector<LexError> errors;

    explicit Scanner(std::string_view s) noexcept : src(s) {}

    [[nodiscard]] bool eof() const noexcept {
        return pos >= src.size();
    }

    [[nodiscard]] char peek(std::size_t offset = 0) const noexcept {
        if (pos + offset >= src.size()) return '\0';
        return src[pos + offset];
    }

    void advance() noexcept {
        if (eof()) return;
        if (src[pos] == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        ++pos;
    }

    SourceSpan start_span() const noexcept {
        return SourceSpan{ /*start*/ pos, /*end*/ pos, line, col };
    }

    void emit(TokenKind k, SourceSpan s) {
        Token t;
        t.kind = k;
        t.span = s;
        tokens.push_back(std::move(t));
    }

    void emit_number(SourceSpan s, std::uint32_t end_pos, double value) {
        Token t;
        t.kind = TokenKind::Number;
        t.span = s;
        t.span.end = end_pos;
        t.lexeme = std::string(src.substr(s.start, end_pos - s.start));
        t.number_value = value;
        tokens.push_back(std::move(t));
    }

    void emit_lexeme(TokenKind k, SourceSpan s, std::uint32_t end_pos) {
        Token t;
        t.kind = k;
        t.span = s;
        t.span.end = end_pos;
        t.lexeme = std::string(src.substr(s.start, end_pos - s.start));
        tokens.push_back(std::move(t));
    }

    void error(SourceSpan s, const std::string& msg) {
        LexError e;
        e.span = s;
        e.message = msg;
        errors.push_back(std::move(e));
    }

    void skip_whitespace_and_comments() noexcept {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                advance();
            } else if (c == '/' && peek(1) == '/') {
                while (!eof() && peek() != '\n') advance();
            } else if (c == '/' && peek(1) == '*') {
                advance(); advance();
                while (!eof() && !(peek() == '*' && peek(1) == '/')) advance();
                if (!eof()) { advance(); advance(); }
            } else {
                break;
            }
        }
    }

    bool scan_number() {
        const auto span = start_span();
        // numbers: ([0-9]+ ('.' [0-9]+)? ([eE] [+-]? [0-9]+)?)
        std::string buf;
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
            buf.push_back(peek());
            advance();
        }
        if (!eof() && peek() == '.' && peek(1) != '.') {
            buf.push_back('.');
            advance();
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    buf.push_back(peek());
                    advance();
                }
            }
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            buf.push_back(peek());
            advance();
            if (!eof() && (peek() == '+' || peek() == '-')) {
                buf.push_back(peek());
                advance();
            }
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) {
                error(span, "malformed numeric literal (no digits after exponent)");
                return false;
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
                buf.push_back(peek());
                advance();
            }
        }
        double value = 0.0;
        try {
            value = std::stod(buf);
        } catch (...) {
            error(span, "invalid numeric literal: '" + buf + "'");
            return false;
        }
        emit_number(span, pos, value);
        return true;
    }

    bool scan_string() {
        const auto span = start_span();
        advance(); // opening "
        std::string buf;
        while (!eof() && peek() != '"') {
            if (peek() == '\\' && peek(1) != '\0') {
                advance();
                const char esc = peek();
                advance();
                switch (esc) {
                    case 'n': buf.push_back('\n'); break;
                    case 't': buf.push_back('\t'); break;
                    case 'r': buf.push_back('\r'); break;
                    case '"': buf.push_back('"'); break;
                    case '\\': buf.push_back('\\'); break;
                    default: buf.push_back(esc); break;
                }
            } else {
                buf.push_back(peek());
                advance();
            }
        }
        if (eof()) {
            error(span, "unterminated string literal");
            return false;
        }
        advance(); // closing "
        Token t;
        t.kind = TokenKind::String;
        t.span = span;
        t.span.end = pos;
        t.lexeme = std::move(buf);
        tokens.push_back(std::move(t));
        return true;
    }

    bool scan_identifier() {
        const auto span = start_span();
        std::string buf;
        // Start with a letter, underscore, or $.
        const char c0 = peek();
        if (!(std::isalpha(static_cast<unsigned char>(c0)) || c0 == '_' || c0 == '$')) {
            error(span, std::string("expected identifier, got '") + c0 + "'");
            return false;
        }
        buf.push_back(c0);
        advance();
        while (!eof()) {
            const char c = peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
                buf.push_back(c);
                advance();
            } else {
                break;
            }
        }
        if (buf == "true") {
            Token t;
            t.kind = TokenKind::BoolTrue;
            t.span = span;
            t.span.end = pos;
            t.lexeme = std::move(buf);
            tokens.push_back(std::move(t));
        } else if (buf == "false") {
            Token t;
            t.kind = TokenKind::BoolFalse;
            t.span = span;
            t.span.end = pos;
            t.lexeme = std::move(buf);
            tokens.push_back(std::move(t));
        } else {
            Token t;
            t.kind = TokenKind::Identifier;
            t.span = span;
            t.span.end = pos;
            t.lexeme = std::move(buf);
            tokens.push_back(std::move(t));
        }
        return true;
    }

    void scan_punct_or_op() {
        const auto span = start_span();
        const char c = peek();
        const char c1 = peek(1);
        switch (c) {
            case '(': advance(); emit(TokenKind::LParen, span); return;
            case ')': advance(); emit(TokenKind::RParen, span); return;
            case '[': advance(); emit(TokenKind::LBracket, span); return;
            case ']': advance(); emit(TokenKind::RBracket, span); return;
            case ',': advance(); emit(TokenKind::Comma, span); return;
            case '.': advance(); emit(TokenKind::Dot, span); return;
            case '+': advance(); emit(TokenKind::Plus, span); return;
            case '-':
                if (c1 == '>') { advance(); advance(); emit(TokenKind::Arrow, span); }
                else          { advance();                emit(TokenKind::Minus, span); }
                return;
            case '*': advance(); emit(TokenKind::Star, span); return;
            case '/': advance(); emit(TokenKind::Slash, span); return;
            case '%': advance(); emit(TokenKind::Percent, span); return;
            case '?': advance(); emit(TokenKind::Question, span); return;
            case ':': advance(); emit(TokenKind::Colon, span); return;
            case '<':
                if (c1 == '=') { advance(); advance(); emit(TokenKind::LessEq, span); }
                else          { advance();                emit(TokenKind::Less, span); }
                return;
            case '>':
                if (c1 == '=') { advance(); advance(); emit(TokenKind::GreaterEq, span); }
                else          { advance();                emit(TokenKind::Greater, span); }
                return;
            case '=':
                if (c1 == '=') { advance(); advance(); emit(TokenKind::EqEq, span); }
                else { error(span, "unexpected '=' (did you mean '=='?)"); advance(); }
                return;
            case '!':
                if (c1 == '=') { advance(); advance(); emit(TokenKind::NotEq, span); }
                else          { advance();                emit(TokenKind::Bang, span); }
                return;
            case '&':
                if (c1 == '&') { advance(); advance(); emit(TokenKind::AndAnd, span); }
                else { error(span, "unexpected '&' (did you mean '&&'?)"); advance(); }
                return;
            case '|':
                if (c1 == '|') { advance(); advance(); emit(TokenKind::OrOr, span); }
                else { error(span, "unexpected '|' (did you mean '||'?)"); advance(); }
                return;
        }
        error(span, std::string("unexpected character '") + c + "'");
        advance();
    }
};

} // namespace

LexResult lex(std::string_view source) noexcept {
    LexResult out;
    Scanner s(source);

    s.skip_whitespace_and_comments();

    while (!s.eof()) {
        const char c = s.peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (!s.scan_number()) {
                // error already pushed; recover by skipping one char
                s.advance();
            }
        } else if (c == '"') {
            if (!s.scan_string()) {
                // already errored; recover
                if (!s.eof()) s.advance();
            }
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '$') {
            if (!s.scan_identifier()) {
                if (!s.eof()) s.advance();
            }
        } else {
            s.scan_punct_or_op();
        }
        s.skip_whitespace_and_comments();
    }

    Token eof;
    eof.kind = TokenKind::Eof;
    eof.span.start = s.pos;
    eof.span.end = s.pos;
    eof.span.line = s.line;
    eof.span.col = s.col;
    s.tokens.push_back(std::move(eof));

    out.tokens = std::move(s.tokens);
    out.errors = std::move(s.errors);
    return out;
}

} // namespace chronon3d::expressions::v2
