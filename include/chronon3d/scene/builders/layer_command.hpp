#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace chronon3d {

class LayerBuilder;

/// Abstract interface for a reusable layer command that can be applied
/// to a `LayerBuilder`.  Commands are the extension mechanism for
/// layer building — features register commands instead of adding
/// methods to LayerBuilder directly.
///
/// A command is parameterized: it may require additional arguments
/// when invoked.  The `apply()` method receives a `LayerBuilder&`
/// which it can chain methods on.
class LayerCommand {
public:
    virtual ~LayerCommand() = default;

    /// Unique identifier for this command (e.g. "motion:slide_in").
    [[nodiscard]] virtual std::string_view id() const = 0;

    /// Human-readable category for grouping (e.g. "motion_preset", "layout", "effect").
    [[nodiscard]] virtual std::string_view category() const = 0;

    /// Human-readable display name (e.g. "Slide In").
    [[nodiscard]] virtual std::string_view display_name() const = 0;

    /// Apply this command to the given builder.  Implementations should
    /// chain builder methods to achieve the desired effect.
    virtual void apply(LayerBuilder& builder) const = 0;
};

/// Convenience: a simple lambda-based command for quick registration.
class LambdaLayerCommand final : public LayerCommand {
public:
    using ApplyFn = std::function<void(LayerBuilder&)>;

    LambdaLayerCommand(std::string id,
                       std::string category,
                       std::string display_name,
                       ApplyFn fn)
        : m_id(std::move(id))
        , m_category(std::move(category))
        , m_display_name(std::move(display_name))
        , m_fn(std::move(fn))
    {}

    [[nodiscard]] std::string_view id() const override { return m_id; }
    [[nodiscard]] std::string_view category() const override { return m_category; }
    [[nodiscard]] std::string_view display_name() const override { return m_display_name; }

    void apply(LayerBuilder& builder) const override { m_fn(builder); }

private:
    std::string m_id;
    std::string m_category;
    std::string m_display_name;
    ApplyFn     m_fn;
};

} // namespace chronon3d
