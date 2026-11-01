/* core.cpp */

#include "core/core.hpp"
#include "compiler/driver/compiler_instance.hpp"

#include <cstdlib>

/* */
int exec_compiler(int argc, char **argv, CompilerMode mode) {
    // TODO
    CompilerInstance compiler;

    if (compiler.run(argc, argv, mode)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* */
int install_lib() {
    // TODO
    return -1;
}

/* */
int uninstall_lib() {
    // TODO
    return -1;
}

/* */
int test_compiler() {
    // TODO
    return -1;
}

/* */
int OpticCore::dispatch_command(Command command, int argc, char **argv) {
    if (command == Command::Help) {
        CompilerInstance::call_usage(true);
        return EXIT_SUCCESS;
    }

    switch (command) {
        case Command::Test:
            break;
        case Command::Install:
            break;
        case Command::Uninstall:
            break;
        case Command::Build:
            return exec_compiler(argc, argv, CompilerMode::Build);
        case Command::Run:
            return exec_compiler(argc, argv, CompilerMode::Run);
        case Command::Debug:
            return exec_compiler(argc, argv, CompilerMode::Debug);
    }

    return EXIT_FAILURE;
}

OpticCore::Command OpticCore::get_command(const char *current_arg) {
    auto it = command_table.find(current_arg);
    return (it != command_table.end()) ? it->second : Command::Unknown;
}

/* */
int OpticCore::start_core(int argc, char *argv[]) {
    Command main_command = get_command(argv[0]);

    if (main_command == Command::Unknown) {
        printf(
            "Bad usage. Argument '%s' is not a main argument.\n" \
            "See a list of all main optic's main arguments.\n\n"
        , argv[0]);
        CompilerInstance::call_usage();
        return EXIT_FAILURE;
    }

    return dispatch_command(main_command, argc, argv);
}