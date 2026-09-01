// ---------------------------------------------------------------------------
// src/ipc/contract_validator_registry.cpp
// ---------------------------------------------------------------------------

#include "contract_validator_registry.hpp"
#include "contract_validator_schemas.hpp"

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace std {
template <>
struct hash<chronon3d::ipc::ContractId> {
    size_t operator()(chronon3d::ipc::ContractId value) const noexcept {
        return static_cast<size_t>(value);
    }
};
} // namespace std

namespace chronon3d::ipc {
namespace {

using Validator = nlohmann::json_schema::json_validator;

struct ErrorHandler final : nlohmann::json_schema::basic_error_handler {
    std::string message;

    void error(const nlohmann::json::json_pointer& pointer,
               const nlohmann::json& instance,
               const std::string& detail) override {
        nlohmann::json_schema::basic_error_handler::error(pointer, instance, detail);
        if (!message.empty()) message += "; ";
        message += pointer.empty() ? "<root>" : pointer.to_string();
        message += ": ";
        message += detail;
    }
};

std::string_view embedded_schema(ContractId contract) noexcept {
    switch (contract) {
        case ContractId::CompositionV1: return detail::kCompositionV1Schema;
        case ContractId::RenderPlanV2: return detail::kRenderPlanV2Schema;
        case ContractId::RenderSettingsV1: return detail::kRenderSettingsV1Schema;
    }
    return {};
}

std::unique_ptr<Validator> compile_schema(ContractId contract) {
    const auto source = embedded_schema(contract);
    if (source.empty()) {
        throw std::runtime_error("ContractValidatorRegistry: schema is not registered");
    }

    try {
        return std::make_unique<Validator>(nlohmann::json::parse(source));
    } catch (const std::exception& error) {
        throw std::runtime_error(
            "ContractValidatorRegistry: cannot compile " +
            std::string(ContractValidatorRegistry::contract_name(contract)) +
            ": " + error.what());
    }
}

} // namespace

struct ContractValidatorRegistry::Impl {
    std::unordered_map<ContractId, std::unique_ptr<Validator>> validators;
};

} // namespace chronon3d::ipc

namespace chronon3d::ipc {

ContractValidatorRegistry::ContractValidatorRegistry()
    : m_impl(std::make_unique<Impl>()) {
    for (const auto contract : {
             ContractId::CompositionV1,
             ContractId::RenderPlanV2,
             ContractId::RenderSettingsV1}) {
        m_impl->validators.emplace(contract, compile_schema(contract));
    }
}

ContractValidatorRegistry::~ContractValidatorRegistry() = default;
ContractValidatorRegistry::ContractValidatorRegistry(ContractValidatorRegistry&&) noexcept = default;
ContractValidatorRegistry& ContractValidatorRegistry::operator=(ContractValidatorRegistry&&) noexcept = default;

bool ContractValidatorRegistry::validate(
    ContractId contract,
    const nlohmann::json& document,
    ContractValidationError* error) const {
    const auto it = m_impl->validators.find(contract);
    if (it == m_impl->validators.end()) {
        if (error) {
            error->contract = std::string(contract_name(contract));
            error->message = "validator is not registered";
        }
        return false;
    }

    try {
        ErrorHandler handler;
        it->second->validate(document, handler);
        if (handler) {
            if (error) {
                error->contract = std::string(contract_name(contract));
                error->message = handler.message;
            }
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        if (error) {
            error->contract = std::string(contract_name(contract));
            error->message = exception.what();
        }
        return false;
    }
}

bool ContractValidatorRegistry::validate_json(
    ContractId contract,
    std::string_view document,
    ContractValidationError* error) const {
    try {
        return validate(contract, nlohmann::json::parse(document), error);
    } catch (const std::exception& exception) {
        if (error) {
            error->contract = std::string(contract_name(contract));
            error->message = std::string("invalid JSON: ") + exception.what();
        }
        return false;
    }
}

std::size_t ContractValidatorRegistry::compiled_validator_count() const noexcept {
    return m_impl->validators.size();
}

std::string_view ContractValidatorRegistry::contract_name(ContractId contract) noexcept {
    switch (contract) {
        case ContractId::CompositionV1: return "chronon.composition.v1";
        case ContractId::RenderPlanV2: return "chronon.render-plan.v2";
        case ContractId::RenderSettingsV1: return "chronon.render-settings.v1";
    }
    return "unknown";
}

const ContractValidatorRegistry& builtin_contract_validators() {
    static const ContractValidatorRegistry registry;
    return registry;
}

} // namespace chronon3d::ipc
