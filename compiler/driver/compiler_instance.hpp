/**
 * @file compiler_instance.hpp
 * @brief Compiler configuration and frontend phase coordination.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/driver/module.hpp"

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace optic::compiler::driver {

using ArgValue = std::variant<
    bool,
    int,
    double,
    std::string
>;

enum class CompilerMode {
    Build,  // Request a native artifact (backend pending)
    Run,    // Request bytecode execution (backend/runtime pending)
    Debug   // Inspect configuration and preprocessed tokens; no debugger
};

/* */
struct TargetMachineInfo {
    // TODO
};

struct CompilerConfig {
    CompilerMode mode = CompilerMode::Build;
    std::vector<std::string> input_files;   // probably delete this to optimize memory

    std::unordered_map<std::string, ArgValue> options = {
        {"--color-diagnostics", "auto"},    // [auto, enable, disable] default=auto
        // more options soon
    };

    std::unordered_map<std::string, bool> flags = {
        {"-Wall", false},
        {"-g",    false},
        // more flags soon
    };


    [[nodiscard]]
    bool has_flag(const std::string& name) const noexcept {
        const auto it = flags.find(name);

        return it != flags.end() && it->second;
    }

    void set_flag(const std::string& name, bool enabled) {
        flags[name] = enabled;
    }

    [[nodiscard]]
    const ArgValue* get_option(const std::string& name) const noexcept {
        const auto it = options.find(name);

        if (it == options.end()) {
            return nullptr;
        }

        return &it->second;
    }

    void set_option(const std::string& name, ArgValue value) {
        options[name] = std::move(value);
    }
};


class CompilerContext {
public:
    CompilerContext() = default;

    CompilerContext(const CompilerContext&) = delete;
    CompilerContext& operator=(
        const CompilerContext&
    ) = delete;

    CompilerContext(CompilerContext&&) = delete;
    CompilerContext& operator=(
        CompilerContext&&
    ) = delete;

    [[nodiscard]]
    CompilerConfig& config() noexcept {
        return compiler_config;
    }

    [[nodiscard]]
    const CompilerConfig& config() const noexcept {
        return compiler_config;
    }

    [[nodiscard]]
    DiagnosticsEngine& diagnostics() noexcept {
        return diagnostics_engine;
    }

    [[nodiscard]]
    ModuleGraph& modules() noexcept {
        return module_graph;
    }

    [[nodiscard]]
    TargetMachineInfo& target() noexcept {
        return target_info;
    }

private:
    CompilerConfig    compiler_config;
    DiagnosticsEngine diagnostics_engine;
    ModuleGraph       module_graph;
    TargetMachineInfo target_info;
};

/* */
class CompilerInstance {
private:
    void cli_tooling(
        int argc,
        char **argv)        noexcept;
    void preprocess()       noexcept;
    void parsing()          noexcept;
    void semantic_analize() noexcept;
    void optimize()         noexcept;
    void linking()          noexcept;
    void code_generation()  noexcept;

    CompilerContext     compiler_context;

public:
    /// Analyze argc compiler arguments. argv excludes the program name and mode.
    /// Debug succeeds after frontend inspection; Build/Run fail until a backend exists.
    [[nodiscard]] int run(int argc, char **argv, CompilerMode) noexcept;
    [[noreturn]]  void stop(bool core_dump=false) noexcept;

    static void call_usage(bool help_manual=false) noexcept;

    void set_mode  (const CompilerMode mode);
    void add_file  (const std::string file);
    void set_option(const std::string option_name, const ArgValue val);
    void set_flag  (const std::string flag_name, const bool val);

    int get_flag_value(const std::string& flag_name) const;
    CompilerMode get_compiler_mode() const;
    std::optional<ArgValue> get_option_value(
        const std::string& option_name
    ) const;
};

} // namespace optic::compiler::driver
