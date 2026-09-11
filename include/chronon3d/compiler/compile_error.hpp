#pragma once

#include <string>
#include <utility>

namespace chronon3d::compiler {

enum class CompileErrorCode { InvalidInput, InvalidProgram, Unsupported, Internal };

struct CompileError {
    CompileErrorCode code{CompileErrorCode::Internal};
    std::string path;
    std::string message;
    std::string component;
};

// Compiler compatibility boundary: CompileErrorCode is retained as the
// domain detail payload while all new error creation uses this adapter.
[[nodiscard]] inline CompileError make_compile_error(
    CompileErrorCode code,
    std::string path,
    std::string message,
    std::string component) {
    return CompileError{
        code, std::move(path), std::move(message), std::move(component)};
}

}  // namespace chronon3d::compiler
