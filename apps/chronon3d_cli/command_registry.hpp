#pragma once

#include <chronon3d/chronon3d.hpp>
#include <CLI/App.hpp>
#include "cli_context.hpp"

namespace chronon3d::cli {

using RegisterCliCommandFn = void (*)(CLI::App&, CliContext&);

struct CliCommandDescriptor {
    const char* name;
    const char* description;
    RegisterCliCommandFn register_fn;
};

void register_command(CLI::App& app, const CliCommandDescriptor& descriptor, CliContext& ctx);
void register_all_commands(CLI::App& app, CliContext& ctx);

} // namespace chronon3d::cli
