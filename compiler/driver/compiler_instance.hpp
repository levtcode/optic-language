/* compiler_instance.hpp */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/driver/module.hpp"

#include <vector>
#include <unordered_map>
#include <variant>

using ArgValue = std::variant<bool, int, double, std::string>;

bool get_data(const std::string fname, std::string &dest, DiagnosticsEngine &diagnostic_engine) noexcept;

enum class CompilerMode {
    Build,  // Builds the source code into binary format
    Run,    // Builds and executes the source code in a runtime engine
    Debug   // Builds and outputs debugging messages to stdout
};

/* */
struct CompilerArgs {
    CompilerMode mode = CompilerMode::Build;
    std::vector<std::string> files;

    std::unordered_map<std::string, ArgValue> options = {
        {"--color-diagnostics", "auto"},    // [auto, enable, disable] default=auto
        // more options soon
    };

    std::unordered_map<std::string, bool> flags = {
        {"-Wall", false},
        {"-g",    false},
        // more flags soon
    };
};

/* */
struct TargetInfo {
    // TODO
};

/* */
class CompilerInstance {
private:
    void cli_tooling(
        int argc,
        char **argv)        noexcept;
    void lexing()           noexcept;
    void preprocess()       noexcept;
    void parsing()          noexcept;
    void semantic_analize() noexcept;
    void optimize()         noexcept;
    void linking()          noexcept;
    void code_generation()  noexcept;

    CompilerArgs        compiler_args;
    DiagnosticsEngine   diagnostics_engine;
    std::vector<Module> modules;

public:
    [[nodiscard]] int run(int argc, char **argv, CompilerMode) noexcept;
    [[noreturn]]  void stop(bool core_dump=false) noexcept;

    static void call_usage(bool help_manual=false) noexcept;

    void set_mode  (const CompilerMode mode);
    void add_file  (const std::string file);
    void set_option(const std::string option_name, const ArgValue val);
    void set_flag  (const std::string flag_name, const bool val);

    int                 get_flag_value(const std::string flag_name);
    CompilerMode        get_compiler_mode();
    ArgValue            get_option_value(const std::string option_name);
    std::vector<Module> get_files();
};