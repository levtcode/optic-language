/* compiler_instance.hpp */

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

using ArgValue = std::variant<
    bool,
    int,
    double,
    std::string
>;

using SourceFileID = std::uint32_t;
inline constexpr SourceFileID invalid_source_file_id = 0;


bool get_data(const std::string fname, std::string &dest, DiagnosticsEngine &diagnostic_engine) noexcept;

enum class CompilerMode {
    Build,  // Builds the source code into binary format
    Run,    // Builds and executes the source code in a runtime engine
    Debug   // Builds and outputs debugging messages to stdout
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


struct SourceFile {
    SourceFileID id = invalid_source_file_id;
    std::filesystem::path path;
    std::string buffer;
};

class SourceManager {
public:
    SourceFileID load_file(const std::filesystem::path& path);
    SourceFileID add_source(std::filesystem::path path, std::string buffer);

    [[nodiscard]]
    const SourceFile* get_file(SourceFileID id) const noexcept;

    [[nodiscard]]
    SourceFile* get_file(SourceFileID id) noexcept;

    [[nodiscard]]
    std::size_t size() const noexcept {
        return source_files.size();
    }

private:
    std::vector<SourceFile> source_files;
    std::unordered_map<std::string, SourceFileID> file_ids;
    SourceFileID next_id = 1;
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
    SourceManager& sources() noexcept {
        return source_manager;
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
    SourceManager     source_manager;
    ModuleGraph       module_graph;
    TargetMachineInfo target_info;
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

    CompilerContext     compiler_context;

public:
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