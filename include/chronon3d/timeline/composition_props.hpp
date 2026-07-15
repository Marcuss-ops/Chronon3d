#pragma once

// ============================================================================
// composition_props.hpp — Input props for Composition factories.
//
// PR 3: Every composition can receive structured input data through
// CompositionProps.  Factories capture what they need; compositions
// remain pure functions of FrameContext.
//
// PR 5: AssetRegistry* added — factories can resolve assets through
// props.require_assets() (returns AssetRegistry&).  Populated
// automatically by CompositionRegistry::create(name) from the
// thread-local registry.
//
// Usage:
//   CompositionProps props;
//   props.values.set("title", "Breaking News");
//   auto comp = registry.create("NewsIntro", props);
//
//   // Inside a factory:
//   registry.add("Example", [](const CompositionProps& props) {
//       auto font = props.require_assets().resolve("fonts/Inter.ttf");
//       ...
//   });
// ============================================================================

#include <chronon3d/core/types/result.hpp>
#include <chronon3d/core/types/types.hpp>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <charconv>
#include <vector>

namespace chronon3d {

class AssetRegistry;

/// Lightweight key-value map for composition input parameters.
///
/// All values are stored as strings.  Typed accessors (get_float, get_int)
/// parse on read.  This keeps the type system simple and serialisation
/// (JSON → ValueMap, Python dict → ValueMap) trivial.
///
/// ── PropsError (decode failure surface) ───────────────────────────────────
///
/// `TypedCompositionDescriptor<Props>::decode` (declared in
/// `composition_descriptor.hpp`) returns `chronon3d::Result<Props, PropsError>`
/// so callers can produce structured errors when CLI/JSON overrides cannot
/// be coerced into typed Props.  The `PropsErrorReason` enum covers the
/// supported v1 reasons per audit §2: string/integer/float/boolean/color/
/// asset path.  Adding reflection-free support for nested arrays/objects
/// is explicitly out of scope (audit §2).
enum class PropsErrorReason {
    MissingRequired,   // key absent in ValueMap
    BadType,           // value present but cannot be parsed to expected type
    OutOfRange,        // value parsed but lies outside the typed field's domain
    InvalidFormat      // asset path / color / other structured-format check failed
};

/// Structured error produced by `TypedCompositionDescriptor::decode` when
/// it cannot merge `CompositionProps::values` into the typed `Props`.
struct PropsError {
    std::string         key;       // offending ValueMap key (empty if general)
    PropsErrorReason    reason;    // reason class
    std::string         message;   // human-readable detail
};

// ═════════════════════════════════════════════════════════════════════════════
//  PropsSchema — declarative schema for typed composition props
// ═════════════════════════════════════════════════════════════════════════════

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

/// Description of a single prop field.  Used to generate documentation,
/// CLI help, JSON schema and example props without reflection.
struct PropField {
    std::string              name;          // machine identifier
    PropType                 type;          // logical type
    bool                     required{false};
    std::string              description;   // human-readable help
    std::optional<std::string> default_value; // stringified default
    std::vector<std::string> enum_values;   // valid enum values when type == Enum
};

/// A collection of prop fields describing the public surface of a Props struct.
struct PropsSchema {
    std::vector<PropField> fields;
};

class ValueMap {
public:
    ValueMap() = default;

    /// Set a key-value pair.
    void set(std::string key, std::string value) {
        m_values[std::move(key)] = std::move(value);
    }

    /// Get a string value.  Returns default_val if the key is missing.
    [[nodiscard]] std::string get_string(std::string_view key,
                                         std::string_view default_val = "") const {
        auto it = m_values.find(key);
        if (it == m_values.end()) return std::string(default_val);
        return it->second;
    }

    /// Get the value for `key`, throwing if the key is not present.
    [[nodiscard]] std::string require_string(std::string_view key) const {
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }
        return it->second;
    }

    /// Get a float value for `key`, throwing if the key is missing or
    /// the value cannot be parsed.
    [[nodiscard]] f32 require_float(std::string_view key) const {
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }
        f32 val = 0.0f;
        auto [ptr, ec] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), val);
        if (ec != std::errc{}) {
            throw std::invalid_argument(
                std::string("ValueMap: cannot parse '" + it->second + "' as float for key: ") + std::string(key));
        }
        return val;
    }

    /// Get an int value for `key`, throwing if the key is missing or
    /// the value cannot be parsed.
    [[nodiscard]] i32 require_int(std::string_view key) const {
        auto it = m_values.find(key);
        if (it == m_values.end()) {
            throw std::out_of_range(
                std::string("ValueMap: missing required key: ") + std::string(key));
        }
        i32 val = 0;
        auto [ptr, ec] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), val);
        if (ec != std::errc{}) {
            throw std::invalid_argument(
                std::string("ValueMap: cannot parse '" + it->second + "' as int for key: ") + std::string(key));
        }
        return val;
    }

    /// Get a float value.  Returns default_val if the key is missing or
    /// the value cannot be parsed.
    [[nodiscard]] f32 get_float(std::string_view key, f32 default_val = 0.0f) const {
        auto it = m_values.find(key);
        if (it == m_values.end()) return default_val;
        f32 val = default_val;
        auto [ptr, ec] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), val);
        if (ec != std::errc{}) return default_val;
        return val;
    }

    /// Get an int value.  Returns default_val if the key is missing or
    /// the value cannot be parsed.
    [[nodiscard]] i32 get_int(std::string_view key, i32 default_val = 0) const {
        auto it = m_values.find(key);
        if (it == m_values.end()) return default_val;
        i32 val = default_val;
        auto [ptr, ec] = std::from_chars(
            it->second.data(), it->second.data() + it->second.size(), val);
        if (ec != std::errc{}) return default_val;
        return val;
    }

    /// Check if a key exists.
    [[nodiscard]] bool contains(std::string_view key) const noexcept {
        return m_values.contains(key);
    }

    /// Number of entries.
    [[nodiscard]] std::size_t size() const noexcept { return m_values.size(); }

    /// True when empty.
    [[nodiscard]] bool empty() const noexcept { return m_values.empty(); }

private:
    std::map<std::string, std::string, std::less<>> m_values;
};


/// Props passed to a Composition factory.
///
/// Factories capture the pieces they need (title text, image paths, etc.)
/// and bake them into the Composition's SceneFunction via lambda capture.
///
/// `assets` is populated automatically by `CompositionRegistry::create(name)`
/// from the thread-local registry, or can be set explicitly by the caller.
///
///     registry.add("NewsIntro", [](const CompositionProps& props) {
///         auto title = props.values.require_string("title");
///         auto font_path = props.require_assets().resolve("fonts/Inter.ttf");
///         return composition({...}, [title, font_path](const FrameContext& ctx) {
///             // use title and font_path
///         });
///     });
struct CompositionProps {
    ValueMap                   values;
    std::filesystem::path      project_root;
    AssetRegistry*             assets = nullptr;

    /// Convenience: return the AssetRegistry reference.
    /// Throws std::logic_error if assets is null.
    [[nodiscard]] AssetRegistry& require_assets() const {
        if (!assets) {
            throw std::logic_error(
                "CompositionProps::require_assets(): assets pointer is null. "
                "Ensure the composition is created via registry.create(name) "
                "or set props.assets explicitly.");
        }
        return *assets;
    }
};

// ═════════════════════════════════════════════════════════════════════════════
//  PropsCodec<Props> — typed decode/encode + schema for a Props struct
// ═════════════════════════════════════════════════════════════════════════════

/// Codec that bridges the untyped `ValueMap` (CLI/JSON input) and a typed
/// `Props` struct.  Carries a declarative schema for introspection plus
/// decode/encode functions for round-trip conversion.
template <typename Props>
struct PropsCodec {
    PropsSchema schema;
    std::function<Result<Props, PropsError>(const ValueMap&, const Props&)> decode;
    std::function<ValueMap(const Props&)> encode;
};

} // namespace chronon3d
