#include "../../commands.hpp"

#include <spdlog/spdlog.h>

#include <iostream>
#include <string>

namespace chronon3d::cli {

namespace {

const char* prop_type_name(PropType type) {
    switch (type) {
        case PropType::String:  return "string";
        case PropType::Integer: return "integer";
        case PropType::Float:   return "float";
        case PropType::Boolean: return "boolean";
        case PropType::Enum:    return "enum";
        case PropType::Color:   return "color";
        case PropType::Path:    return "path";
    }
    return "unknown";
}

void print_metadata(const CompositionDescriptor& descriptor) {
    std::cout << "ID:          " << descriptor.id << '\n';
    std::cout << "Category:    "
              << (descriptor.category.empty() ? "Uncategorized" : descriptor.category)
              << '\n';

    std::cout << "Dimensions:  ";
    if (descriptor.width && descriptor.height) {
        std::cout << *descriptor.width << "×" << *descriptor.height << '\n';
    } else {
        std::cout << "dynamic / not declared\n";
    }

    std::cout << "FPS:         ";
    if (descriptor.fps) {
        std::cout << descriptor.fps->numerator << '/'
                  << descriptor.fps->denominator << '\n';
    } else {
        std::cout << "dynamic / not declared\n";
    }

    std::cout << "Duration:    ";
    if (descriptor.duration) {
        std::cout << descriptor.duration->integral() << " frames\n";
    } else {
        std::cout << "dynamic / not declared\n";
    }
}

void print_props(const CompositionDescriptor& descriptor) {
    std::cout << "Props:\n";
    if (!descriptor.schema || descriptor.schema->fields.empty()) {
        std::cout << "  (none declared)\n";
        return;
    }

    for (const auto& field : descriptor.schema->fields) {
        std::cout << "  " << field.name << "\n"
                  << "    type:        " << prop_type_name(field.type) << '\n'
                  << "    required:    " << (field.required ? "yes" : "no") << '\n'
                  << "    default:     "
                  << (field.default_value ? *field.default_value : "(none)") << '\n';
        if (!field.enum_values.empty()) {
            std::cout << "    constraints: ";
            for (std::size_t i = 0; i < field.enum_values.size(); ++i) {
                if (i > 0) std::cout << " | ";
                std::cout << field.enum_values[i];
            }
            std::cout << '\n';
        } else {
            std::cout << "    constraints: "
                      << (field.required ? "required" : "schema/codec validation")
                      << '\n';
        }
        if (!field.description.empty()) {
            std::cout << "    description: " << field.description << '\n';
        }
    }
}

} // namespace

int command_info(const CompositionRegistry& registry, const std::string& id) {
    const auto descriptor = registry.descriptor_of(id);
    if (!descriptor) {
        spdlog::error("Unknown composition: {}", id);
        return 1;
    }

    print_metadata(*descriptor);
    print_props(*descriptor);
    std::cout << "Assets:      resolved by `chronon validate " << descriptor->id
              << " [--props-file props.json]`\n";
    return 0;
}

} // namespace chronon3d::cli
