#pragma once

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>

#include <charconv>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chronon3d {

class AssetRegistry;

/// Structured failure returned by PropsCodec decode.
enum class PropsErrorReason {
    MissingRequired,
    BadType,
    OutOfRange,
    InvalidFormat
};

struct PropsError {
    std::string key;
    PropsErrorReason reason;
    std::string message;
};

/// Supported prop types for schema introspection and CLI/JSON validation.
enum class PropType {
    String,
    Integer,
    Float,
    Boolean,
    Enum,
    Color,
    Path
};

struct PropField {
    std::string name;
    PropType type;
    bool required{false};
    std::string description;
    std::optional<std::string> default_value;
    std::vector<std::string> enum_values;
};

struct PropsSchema {
    std::vector<PropField> fields;
};

/// Lightweight string map used at CLI, JSON and C-ABI boundaries.
class ValueMap {
public:
    ValueMap() = default;

    void set(std::string key, std::string value) {
        values_[std::move(key)] = std::move(value);
    }

    [[nodiscard]] std::string get_string(
        std::string_view key,
        std::string_view default_value = "") const {
        const auto it = values_.find(key);
        return it == values_.end()
            ? std::string(default_value)
            : it->second;
    }

    [[nodiscard]] std::string require_string(std::string_view key) const {
        const auto it = values_.find(key);
        if (it == values_.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }
        return it->second;
    }

    [[nodiscard]] f32 require_float(std::string_view key) const {
        const auto it = values_.find(key);
        if (it == values_.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }

        f32 value = 0.0f;
        const auto [ptr, error] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), value);
        if (error != std::errc{} || ptr != it->second.data() + it->second.size()) {
            throw std::invalid_argument(
                "ValueMap: cannot parse '" + it->second +
                "' as float for key: " + std::string(key));
        }
        return value;
    }

    [[nodiscard]] i32 require_int(std::string_view key) const {
        const auto it = values_.find(key);
        if (it == values_.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }

        i32 value = 0;
        const auto [ptr, error] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), value);
        if (error != std::errc{} || ptr != it->second.data() + it->second.size()) {
            throw std::invalid_argument(
                "ValueMap: cannot parse '" + it->second +
                "' as int for key: " + std::string(key));
        }
        return value;
    }

    [[nodiscard]] f32 get_float(std::string_view key,
                                f32 default_value = 0.0f) const {
        const auto it = values_.find(key);
        if (it == values_.end()) return default_value;

        f32 value = default_value;
        const auto [ptr, error] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), value);
        if (error != std::errc{} || ptr != it->second.data() + it->second.size()) {
            return default_value;
        }
        return value;
    }

    [[nodiscard]] i32 get_int(std::string_view key,
                              i32 default_value = 0) const {
        const auto it = values_.find(key);
        if (it == values_.end()) return default_value;

        i32 value = default_value;
        const auto [ptr, error] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), value);
        if (error != std::errc{} || ptr != it->second.data() + it->second.size()) {
            return default_value;
        }
        return value;
    }

    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return values_.contains(key);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return values_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return values_.empty();
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

/// External input passed to a composition descriptor.
///
/// Canonical registration uses CompositionDescriptor or
/// TypedCompositionDescriptor<Props>. PropsCodec owns typed decoding; factories
/// receive data only after CompositionRegistry has completed prepare_props.
struct CompositionProps {
    ValueMap values;
    std::filesystem::path project_root;
    AssetRegistry* assets{nullptr};

    [[nodiscard]] AssetRegistry& require_assets() const {
        if (!assets) {
            throw std::logic_error(
                "CompositionProps::require_assets(): assets pointer is null. "
                "Create the composition through CompositionRegistry or set "
                "CompositionProps::assets explicitly.");
        }
        return *assets;
    }
};

/// Single typed bridge between ValueMap and a composition Props structure.
template <typename Props>
struct PropsCodec {
    PropsSchema schema;
    std::function<Result<Props, PropsError>(const ValueMap&, const Props&)> decode;
    std::function<ValueMap(const Props&)> encode;
};

} // namespace chronon3d
