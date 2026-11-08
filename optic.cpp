/**
 * @file optic.cpp
 * @brief Process entry point for the Optic command-line coordinator.
 */
#include "cli/cli_tool.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        return optic::cli::CliTool{}.run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "optic: " << error.what() << '\n';
        return 1;
    }
}
