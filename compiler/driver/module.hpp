/* module.hpp */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/lexer/tokens.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using ModuleID = std::uint32_t;

constexpr        std::string_view OPTIC_EXTENSION = ".optic";
inline constexpr ModuleID invalid_module_id = 0;

/// There is a semantic difference between an Optic's file and an Optic's module.

/// Optic's file is just an IO buffer, but Optic's module is a conceptual object that represents
/// an Optic's source code, with dependencies, source location and his buffer.


class Module {
private:
    std::filesystem::path path;
    std::string           buffer;
    SourceLocation        module_location;
    std::vector<token_t>  tokens;
    std::vector<ModuleID> dependencies;

public:
    explicit Module(std::string path)
        : path(std::move(path)) {}

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    ~Module() = default;

    std::string get_path() const noexcept {
        return path.generic_string();
    }

    std::string_view get_buffer() const noexcept {
        return buffer;
    }

    SourceLocation& loc() noexcept {
        return module_location;
    }

    const SourceLocation& loc() const noexcept {
        return module_location;
    }

    std::vector<token_t>& get_tokens() noexcept {
        return tokens;
    }

    const std::vector<token_t>& get_tokens() const noexcept {
        return tokens;
    }

    const std::vector<ModuleID>&
    get_dependencies() const noexcept {
        return dependencies;
    }

    void set_buffer(std::string buffer) {
        this->buffer = std::move(buffer);
    }

    void add_dependency(const ModuleID &dependency) {
        dependencies.push_back(std::move(dependency));
    }
};

class ModuleGraph {
public:
    ModuleID add_module(std::unique_ptr<Module> module);

    [[nodiscard]]
    Module* get_module(ModuleID id) noexcept;

    bool add_dependency(ModuleID module, ModuleID dependency);

    [[nodiscard]]
    const std::vector<ModuleID>&
    compilation_order() const noexcept {
        return ordered_modules;
    }

    [[nodiscard]]
    std::size_t size() const noexcept {
        return modules.size();
    }

private:
    std::unordered_map<ModuleID, std::unique_ptr<Module>> modules;
    std::vector<ModuleID> ordered_modules;
    ModuleID next_id = 1;
};