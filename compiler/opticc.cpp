/**
 * @file opticc.cpp
 * @brief Standalone compiler entry point; accepts a mode and compiler arguments.
 *
 * debug currently inspects the frontend. build/run report unavailable backend
 * functionality rather than claiming to have generated or executed an artifact.
 */
#include "compiler/driver/compiler_instance.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        optic::compiler::driver::CompilerInstance::call_usage();
        return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "help" || command == "--help" || command == "-h") {
        optic::compiler::driver::CompilerInstance::call_usage();
        return argc == 2 ? 0 : 2;
    }
    if (command == "--version") {
        std::cout << "opticc " << OPTIC_VERSION << '\n';
        return argc == 2 ? 0 : 2;
    }
    optic::compiler::driver::CompilerMode mode;
    if (command == "build") mode = optic::compiler::driver::CompilerMode::Build;
    else if (command == "run") mode = optic::compiler::driver::CompilerMode::Run;
    else if (command == "debug") mode = optic::compiler::driver::CompilerMode::Debug;
    else {
        std::cerr << "opticc: unknown mode '" << command << "'.\n";
        return 2;
    }
    if (argc == 3 && (std::string_view(argv[2]) == "--help" || std::string_view(argv[2]) == "-h")) {
        optic::compiler::driver::CompilerInstance::call_usage();
        return 0;
    }
    optic::compiler::driver::CompilerInstance compiler;
    // run() receives compiler arguments only: neither executable name nor mode.
    return compiler.run(argc - 2, argv + 2, mode);
}
