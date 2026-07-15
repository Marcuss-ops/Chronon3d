// ═══════════════════════════════════════════════════════════════════════════
// command_example_props.cpp — Phase 1d / Increment D
// ───────────────────────────────────────────────────────────────────────────
//
// `chronon example-props <comp_id>` — emits canonical example props
// (defaults from `descriptor.schema.fields[i].default_value`) as
// machine-readable JSON.  Distinct from `chronon schema` (which emits
// the schema metadata: types, required, enum constraints); `example-props`
// emits the VALUE you'd pass to `--props-file` to start from defaults.
//
// Wraps canonical `CompositionRegistry::descriptor_of(name)` only.
// No `CompositionResolveFn` lambda or any abandoned abstraction.
//
// Exit codes:
//   0 = PASS (descriptor has codec schema + field defaults).
//   1 = FAIL (unknown composition, or descriptor has no codec schema
//             and typed defaults are not introspectable).

#include "../../commands.hpp"

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d {
namespace cli {

int command_example_props(const CompositionRegistry& registry,
                          const ExamplePropsArgs& args) {
    const auto descriptor = registry.descriptor_of(args.comp_id);
    if (!descriptor) {
        spdlog::error("example-props: unknown composition '{}'", args.comp_id);
        nlohmann::json err;
        err["error"]         = "composition_not_found";
        err["composition_id"] = args.comp_id;
        err["status"]        = "FAIL";
        std::cout << err.dump(2) << "\n";
        return 1;
    }

    if (!descriptor->schema || descriptor->schema->fields.empty()) {
        spdlog::error("example-props: no canonical example for '{}' "
                      "(descriptor has no codec schema)",
                      args.comp_id);
        nlohmann::json err;
        err["error"]         = "no_canonical_example";
        err["composition_id"] = args.comp_id;
        err["reason"]        =
            "descriptor has no codec schema; typed defaults are not "
            "introspectable without a registered PropsCodec";
        err["status"]        = "FAIL";
        std::cout << err.dump(2) << "\n";
        return 1;
    }

    nlohmann::json js;
    js["composition_id"] = args.comp_id;
    nlohmann::json props_json = nlohmann::json::object();
    for (const auto& field : descriptor->schema->fields) {
        // Emit the canonical default from descriptor.schema[i].default_value
        // (populated by TypedCompositionDescriptor::to_descriptor() via
        // the codec schema).  Fields without a default are emitted as
        // empty strings so callers know which keys to author manually.
        if (field.default_value) {
            props_json[field.name] = *field.default_value;
        } else {
            props_json[field.name] = std::string{};
        }
    }
    js["props"]          = std::move(props_json);
    std::cout << js.dump(2) << "\n";
    return 0;
}

} // namespace cli
} // namespace chronon3d
