/**
 * @file process.cpp
 * @brief Component discovery and synchronous process execution on POSIX/Windows.
 *
 * POSIX children share the terminal/process group, so terminal signals reach both
 * processes. Interrupted waits are retried. Windows uses the CRT spawn API.
 */
#include "cli/process.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
extern char** environ;
#endif

namespace optic::cli {
namespace {
#ifdef _WIN32
// The CRT joins spawn arguments into a command line; encode each argument for
// the Microsoft C/C++ argv decoder, including quotes and trailing backslashes.
std::string quote_argument(const std::string& argument) {
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (char character : argument) {
        if (character == '\\') {
            ++backslashes;
            continue;
        }
        quoted.append(backslashes * (character == '"' ? 2 : 1), '\\');
        backslashes = 0;
        if (character == '"') {
            quoted += '\\';
        }
        quoted += character;
    }
    quoted.append(backslashes * 2, '\\');
    quoted += '"';
    return quoted;
}
#endif
std::filesystem::path executable_path(const char* argv0) {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size > 0 && size < buffer.size()) {
        return std::wstring(buffer.data(), size);
    }
#elif defined(__linux__)
    std::error_code error;
    auto path = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error) {
        return path;
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return buffer.data();
    }
#endif
    const std::filesystem::path path_from_args(argv0 ? argv0 : "");
    // A bare argv[0] does not establish the executable directory.
    if (path_from_args.has_parent_path()) {
        return std::filesystem::absolute(path_from_args);
    }
    return {};
}
} // namespace

std::string compiler_executable(const char* argv0) {
    if (const char* configured = std::getenv("OPTIC_COMPILER"); configured && *configured) {
        return configured;
    }
#ifdef _WIN32
    constexpr auto name = "opticc.exe";
#else
    constexpr auto name = "opticc";
#endif
    const auto self = executable_path(argv0);
    if (!self.empty()) {
        const auto sibling = self.parent_path() / name;
        std::error_code error;
        if (std::filesystem::exists(sibling, error)) {
            return sibling.string();
        }
    }
    return name;
}

int run_process(const std::string& executable, const std::vector<std::string>& arguments) {
#ifdef _WIN32
    std::vector<std::string> encoded;
    encoded.push_back(quote_argument(executable));
    for (const auto& argument : arguments) {
        encoded.push_back(quote_argument(argument));
    }
    std::vector<const char*> argv;
    for (const auto& argument : encoded) {
        argv.push_back(argument.c_str());
    }
    argv.push_back(nullptr);
    const auto result = _spawnvp(_P_WAIT, executable.c_str(), argv.data());
    if (result != -1) {
        return static_cast<int>(result);
    }
    const int error = errno;
#else
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    pid_t child;
    const int error =
        posix_spawnp(&child, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (error == 0) {
        int status;
        while (waitpid(child, &status, 0) == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "optic: cannot wait for component: " << std::strerror(errno) << '\n';
            return 126;
        }
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return 126;
    }
#endif
    std::cerr << "optic: cannot launch '" << executable << "': " << std::strerror(error) << '\n';
    return error == ENOENT ? 127 : 126;
}

} // namespace optic::cli
