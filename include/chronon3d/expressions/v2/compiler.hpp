// compiler.hpp — Path B expression entry: source -> AST -> bytecode.
//
// Single entry function `compile(source)` produces a `Program` ready for the
// VM to interpret. Plumbing:
//     source -> lex() -> tokens -> parse -> ast -> type_check -> emit.
//
// Compilation is intended to run ONCE per source text; the bytecode is then
// cached/either inside a ProgramCache (caller-supplied) or by the VM/host.
//
// The compiled program is *not* thread-safe for execution concurrently
// because the VM mutates its PC and stack — but compiling itself is
// pure and can run on many threads in parallel for many sources.

#pragma once

#include <chronon3d/expressions/v2/ast.hpp>
#include <chronon3d/expressions/v2/bytecode.hpp>
#include <chronon3d/expressions/v2/lexer.hpp>
#include <chronon3d/expressions/v2/type_checker.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace chronon3d::expressions::v2 {

struct CompileError {
    SourceSpan span{};
    std::string message;
};

struct CompileResult {
    Program                program;
    std::vector<CompileError> errors;

    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

// ── compile(source): run the pipeline from source to bytecode ────────
[[nodiscard]] CompileResult compile(std::string_view source) noexcept;

// ── compile(ast): skip lex/parse; emit bytecode from a pre-built AST. ─
//   Used by callers who already have an AST (e.g. integration with the
//   existing AnimatedValue expression-string path).
[[nodiscard]] CompileResult compile_ast(const AstNode& ast) noexcept;

} // namespace chronon3d::expressions::v2
