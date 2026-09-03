// ---------------------------------------------------------------------------
// src/ipc/contract_validator_registry.hpp
//
// Canonical compiled JSON Schema registry for daemon boundary contracts.
// Schemas are parsed and compiled once when the registry is constructed;
// request validation only invokes the immutable compiled validators.
// ---------------------------------------------------------------------------
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace chronon3d::ipc {

enum class ContractId {
    CompositionV1,
    RenderPlanV2,
    RenderSettingsV1,
};

struct ContractValidationError {
    std::string contract;
    std::string message;
};

class ContractValidatorRegistry {
public:
    /// Compile all built-in schemas once. Throws if a schema is missing or
    /// malformed; a daemon cannot start with an incomplete contract set.
    ContractValidatorRegistry();
    ~ContractValidatorRegistry();

    ContractValidatorRegistry(ContractValidatorRegistry&&) noexcept;
    ContractValidatorRegistry& operator=(ContractValidatorRegistry&&) noexcept;
    ContractValidatorRegistry(const ContractValidatorRegistry&) = delete;
    ContractValidatorRegistry& operator=(const ContractValidatorRegistry&) = delete;

    /// Validate a parsed document against an already-compiled v1 validator.
    /// This operation is const and safe for concurrent request callers.
    [[nodiscard]] bool validate(
        ContractId contract,
        const nlohmann::json& document,
        ContractValidationError* error = nullptr) const;

    /// Validate the JSON text carried by an IPC string field, reporting parse
    /// failures through the same structured error surface.
    [[nodiscard]] bool validate_json(
        ContractId contract,
        std::string_view document,
        ContractValidationError* error = nullptr) const;

    [[nodiscard]] std::size_t compiled_validator_count() const noexcept;
    [[nodiscard]] static std::string_view contract_name(ContractId contract) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Process-local canonical registry for callers that do not own a daemon
/// dispatcher. Initialization is thread-safe and happens exactly once.
[[nodiscard]] const ContractValidatorRegistry& builtin_contract_validators();

} // namespace chronon3d::ipc
