/**
 * @file cli_tool.cpp
 * @brief Command routing and user-facing help for the optic executable.
 */
#include "cli/cli_tool.hpp"
#include "cli/process.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace optic::cli {

void CliTool::print_usage() {
    std::cout << "Usage: optic <command> [arguments]\n\n"
              << "  help, --help     Show this help\n"
              << "  --version        Show the toolchain version\n"
              << "  build <files>    Delegate compilation to opticc (code generation pending)\n"
              << "  run <files>      Delegate to opticc (execution pending)\n"
              << "  debug <files>    Dump compiler configuration and tokens; not a debugger\n"
              << "  install          Package installation (not implemented)\n"
              << "  uninstall        Package removal (not implemented)\n"
              << "  test             Toolchain test command (not implemented)\n\n"
              << "Use optic <build|run|debug> --help for compiler options.\n";
}

int CliTool::run(int argc, char* argv[]) const {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "help" || command == "--help" || command == "-h") {
        if (argc != 2) {
            std::cerr << "optic: help takes no arguments.\n";
            return 2;
        }
        print_usage();
        return 0;
    }
    if (command == "--version") {
        if (argc != 2) {
            return 2;
        }
        std::cout << "optic " << OPTIC_VERSION << '\n';
        return 0;
    }
    if (command == "install" || command == "uninstall" || command == "test") {
        std::cerr << "optic: command '" << command << "' is not implemented.\n";
        return 1;
    }
    if (command != "build" && command != "run" && command != "debug") {
        std::cerr << "optic: unknown command '" << command << "'. See optic --help.\n";
        return 2;
    }
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return run_process(compiler_executable(argv[0]), arguments);
}

} // namespace optic::cli
