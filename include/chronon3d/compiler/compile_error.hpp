#pragma once

#include <string>

namespace chronon3d::compiler {

enum class CompileErrorCode { InvalidInput, InvalidProgram, Unsupported, Internal };

struct CompileError {
    CompileErrorCode code{CompileErrorCode::Internal};
    std::string path;
    std::string message;
    std::string component;
};

}  // namespace chronon3d::compiler
