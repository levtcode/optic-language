/** @file module.hpp
 * @brief Source ownership and the dependency graph shared by frontend phases.
 */
#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/lexer/tokens.hpp"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace optic::compiler::driver {
using ModuleID = std::uint32_t;
inline constexpr ModuleID invalid_module_id = 0;
inline constexpr std::string_view OPTIC_EXTENSION = ".optic";

struct ImportBinding {
    ModuleID target;
    std::string name;  // Written dotted name or path, without rewriting namespace visibility.
    std::string alias; // Empty when no explicit `as` binding was requested.
    unsigned line, column;
};

enum class ModuleState { Loaded, Processing, Ready, Failed };

/// Owns the original source and the parser's token stream. Imports never concatenate sources.
class Module {
public:
    explicit Module(std::string path);
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    std::string get_path() const {
        return path.generic_string();
    }

    std::string_view get_buffer() const noexcept {
        return buffer;
    }

    std::vector<lexer::token_t>& get_tokens() noexcept {
        return tokens;
    }

    const std::vector<lexer::token_t>& get_tokens() const noexcept {
        return tokens;
    }

    const std::vector<ModuleID>& get_dependencies() const noexcept {
        return dependencies;
    }

    const std::vector<ImportBinding>& get_imports() const noexcept {
        return imports;
    }

    void clear_imports() {
        imports.clear();
    }

    void record_import(ImportBinding binding) {
        imports.push_back(std::move(binding));
    }

    void set_buffer(std::string value) {
        buffer = std::move(value);
    }

    ModuleState state() const noexcept {
        return processing_state;
    }

    void set_state(ModuleState value) noexcept {
        processing_state = value;
    }

private:
    friend class ModuleGraph;
    std::filesystem::path path;
    std::string buffer;
    std::vector<lexer::token_t> tokens;
    std::vector<ModuleID> dependencies;
    std::vector<ImportBinding> imports;
    ModuleState processing_state = ModuleState::Loaded;
};

/// Stable module IDs and canonical paths; insertion order breaks ties deterministically.
class ModuleGraph {
public:
    static std::string path_key(const std::filesystem::path& path);
    ModuleID add_module(std::unique_ptr<Module> module);
    ModuleID find_module(const std::filesystem::path& path) const;
    Module* get_module(ModuleID id) noexcept;
    const Module* get_module(ModuleID id) const noexcept;
    /// Reject invalid IDs and cycles without modifying the graph. Duplicate edges are harmless.
    bool add_dependency(ModuleID module, ModuleID dependency);
    /// Rebuild dependency-first order. On failure the order is empty.
    bool topological_sort();

    const std::vector<ModuleID>& compilation_order() const noexcept {
        return ordered_modules;
    }

    std::size_t size() const noexcept {
        return modules.size();
    }

    void print(std::ostream& out) const;

private:
    std::map<ModuleID, std::unique_ptr<Module>> modules;
    std::unordered_map<std::string, ModuleID> path_ids;
    std::vector<ModuleID> ordered_modules;
    ModuleID next_id = 1;
};
} // namespace optic::compiler::driver
