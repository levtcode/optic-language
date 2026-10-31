/* module.hpp */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/lexer/tokens.hpp"

#include <string>
#include <string_view>
#include <vector>

constexpr std::string_view OPTIC_EXTENSION = ".optic";

/// There is a semantic difference between an Optic's file and an Optic's module.

/// Optic's file is just an IO buffer, but Optic's module is a conceptual object that represents
/// an Optic's source code, with dependencies, source location and his buffer.


class Module {
private:
    std::string path;
    std::string buffer;
    std::vector<token_t> tokens;
    std::vector<std::string> dependencies;
    SourceLocation mod_loc;

public:
    explicit Module(std::string path)
        : path(std::move(path)) {}

    explicit Module(std::string path, SourceLocation loc)
        : path(std::move(path)), mod_loc(std::move(loc)) {}

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    Module(Module&&) = delete;
    Module& operator=(Module&&) = delete;

    ~Module() = default;

    std::string_view get_path() const noexcept {
        return path;
    }

    std::string_view get_buffer() const noexcept {
        return buffer;
    }

    const std::vector<token_t>& get_tokens() const noexcept {
        return tokens;
    }

    const std::vector<std::string>&
    get_dependencies() const noexcept {
        return dependencies;
    }

    void set_buffer(std::string buffer) {
        this->buffer = std::move(buffer);
    }

    void add_dependency(std::string dependency) {
        dependencies.push_back(std::move(dependency));
    }
};