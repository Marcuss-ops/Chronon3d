// vm.hpp — Path B expression VM.
//
// Stack-machine interpreter. Owns:
//   * std::vector<ExpressionValue> stack
//   * std::unordered_map<intern-name, ExpressionValue> env
//   * std::uint32_t pc (program counter)
//
// Public surface:
//   * Vm vm{}; vm.set("x", make_number(7));       // bind a variable
//   * ExpressionValue v = vm.run(program, const_<num|str|bool>(slot));
//                          or use the higher-level `evaluate()` below.
//
// run() executes Program::code[pc=0..end] and returns the top-of-stack
// after RETURN. Errors during execution are reported via an optional out-
// parameter (no throws).

#pragma once

#include <chronon3d_experimental/expressions/v2/bytecode.hpp>
#include <chronon3d_experimental/expressions/v2/expression_value.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chronon3d::expressions::v2 {

struct VmError {
    std::uint32_t pc{0};
    std::string message;
};

class Vm {
public:
    // Bind a name to a value (used as LOAD_VAR's source at runtime).
    void set(const std::string& name, ExpressionValue v) { env_[name] = std::move(v); }

    [[nodiscard]] std::optional<ExpressionValue> get(const std::string& name) const {
        auto it = env_.find(name);
        if (it == env_.end()) return std::nullopt;
        return it->second;
    }

    /// Clear all variable bindings.
    ///
    /// **Owner-invariant (DO NOT REGRESS).** The `Vm` class owns only `env_`
    /// as observable instance state today. `reset()` therefore leaves the Vm in
    /// the same observable state as a freshly-constructed Vm.  If a future
    /// contributor adds a new private field with observable state (e.g. a
    /// const-pool cache, a stack snapshot for unwinding, a profiling accumulator),
    /// they MUST update this method to clear every observable field, otherwise
    /// Gate 2's determinism contract is silently broken.
    ///
    /// Required by Opzione B Gate 2 so test runs can isolate runs that share a
    /// long-lived Vm instance (anim systems often hold one across frames).
    void reset() noexcept { env_.clear(); }

    /// Whether env_ is empty (true immediately after construction and after
    /// reset()). Useful as a test-side guard before reusing a Vm.
    [[nodiscard]] bool empty() const noexcept { return env_.empty(); }

    /// Number of distinct bindings currently held in env_.
    [[nodiscard]] std::size_t env_size() const noexcept { return env_.size(); }

    /// Run the program (no throws). Errors populate `err` (if non-null).
    [[nodiscard]] ExpressionValue run(const Program& program, VmError* err = nullptr);

    /// End-to-end: compile + run. Convenience for callers that don't need
    /// to cache the bytecode (e.g. tests).
    [[nodiscard]] static ExpressionValue evaluate(std::string_view source,
                                                  VmError* err = nullptr);

private:
    std::unordered_map<std::string, ExpressionValue> env_;
};

} // namespace chronon3d::expressions::v2
