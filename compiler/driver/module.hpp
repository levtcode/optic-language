/* module.hpp */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/lexer/tokens.hpp"

#include <string>
#include <vector>
#include <filesystem>

constexpr const char *optic_extension = ".optic";

/* */
class Module {
private:
    std::string          name;
    std::string          path;
    std::string          buffer;
    SourceLocation       mod_loc;
    std::vector<token_t> tokens;

    void name_module_format() noexcept {
        std::filesystem::path __path__ = path;
        name = __path__.filename();

        auto pos = name.find_last_of('.');
        if (pos != std::string::npos && name.ends_with(optic_extension)) {
            name.erase(pos);
        }
    }

public:
    Module(const std::string &_path_) : path(_path_) { name_module_format(); mod_loc.file = path; }

    inline std::string get_name() const noexcept { return name; }
    inline std::string get_path() const noexcept { return path; }

    inline std::string          &get_buffer() noexcept { return buffer;  }
    inline SourceLocation       &loc()        noexcept { return mod_loc; }
    inline std::vector<token_t> &get_tokens() noexcept { return tokens;  }
    // more methods here...
};