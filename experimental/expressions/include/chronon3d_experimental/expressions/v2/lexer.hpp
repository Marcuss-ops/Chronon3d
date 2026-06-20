// lexer.hpp — Path B expression lexer.
//
// Tokenises the source string into a span-tagged token stream. The lexer is
// the entry point of the v2 pipeline. It does NOT build the AST; that's the
// parser compiler's job. It just produces tokens for the compiler to walk.

#pragma once

#include <chronon3d/expressions/v2/expression_value.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace chronon3d::expressions::v2 {

// ── SourceSpan: byte range + line/column for diagnostics ─────────────
struct SourceSpan {
    std::uint32_t start{0};
    std::uint32_t end{0};
    std::uint32_t line{1};
    std::uint32_t col{1};

    [[nodiscard]] std::size_t length() const noexcept {
        return end > start ? static_cast<std::size_t>(end - start) : 0;
    }
};

// ── Token kinds ────────────────────────────────────────────────────────
enum class TokenKind : std::uint16_t {
    // Literals
    Number,        // 1, 1.5, 1e2
    String,        // "..."
    Identifier,    // foo, thisLayer, value
    BoolTrue,      // true
    BoolFalse,     // false

    // Punctuation
    LParen,        // (
    RParen,        // )
    LBracket,      // [
    RBracket,      // ]
    Comma,         // ,
    Dot,           // .
    Arrow,         // -> (not used yet; reserved)

    // Arithmetic
    Plus,          // +
    Minus,         // -
    Star,          // *
    Slash,         // /
    Percent,       // %

    // Comparison
    EqEq,          // ==
    NotEq,         // !=
    Less,          // <
    LessEq,        // <=
    Greater,       // >
    GreaterEq,     // >=

    // Logical
    AndAnd,        // &&
    OrOr,          // ||
    Bang,          // !

    // Conditional
    Question,      // ?
    Colon,         // :

    // End
    Eof,
};

[[nodiscard]] const char* token_kind_name(TokenKind k) noexcept;

// ── Token: span + kind + (literal value) ──────────────────────────────
struct Token {
    TokenKind     kind{TokenKind::Eof};
    SourceSpan    span{};
    std::string   lexeme;        // identifier / string literal / etc.

    // For Number literals we also store the parsed value (cheap; avoids
    // re-parsing on compile).
    double        number_value{0.0};
};

[[nodiscard]] std::string to_string(TokenKind k);

// ── Lex: tokenise a source string ─────────────────────────────────────
struct LexError {
    SourceSpan span{};
    std::string message;
};

struct LexResult {
    std::vector<Token> tokens;
    std::vector<LexError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// Numeric mode for un-signed/integer literals (defaults to double).
[[nodiscard]] LexResult lex(std::string_view source) noexcept;

} // namespace chronon3d::expressions::v2
