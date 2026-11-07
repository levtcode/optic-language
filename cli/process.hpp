/**
 * @file process.hpp
 * @brief Locate and execute independent toolchain components without a shell.
 */
#pragma once

#include <string>
#include <vector>

namespace optic::cli {

/// OPTIC_COMPILER overrides discovery. Otherwise prefer opticc beside optic,
/// falling back to PATH. argv0 is a fallback when OS executable discovery fails.
[[nodiscard]] std::string compiler_executable(const char* argv0);

/// Inherit standard input/output/error and wait for the child process.
/// Return its exit code, 128 + signal on POSIX, 127 if missing, or 126 on launch failure.
[[nodiscard]] int run_process(const std::string& executable,
                              const std::vector<std::string>& arguments);

} // namespace optic::cli
