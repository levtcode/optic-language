/**
 * @file cli_tool.hpp
 * @brief Public command-line entry point for the Optic toolchain.
 */
#pragma once

namespace optic::cli {

/// Coordinates commands without linking compiler or runtime implementation code.
class CliTool {
public:
    /// Receives the complete main() argument vector, including the executable name.
    /// Returns 0 on success, 2 for usage errors, or the delegated process status.
    [[nodiscard]] int run(int argc, char* argv[]) const;
    static void print_usage();
};

} // namespace optic::cli
