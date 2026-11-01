/** 
 * @file lexer.hpp
 *
 * @brief Declaration of Optic's lexical analyzer.
 *
 * The lexer is responsible for transforming a source module into a sequence of
 * lexical tokens while preserving source locations for diagnostics.
 *
 * Token recognition is delegated to specialized readers for each lexical
 * category (identifiers, literals, operators, comments, etc.). The resulting
 * token stream is later consumed by the parser.
 */

#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/driver/module.hpp"
#include "compiler/lexer/tokens.hpp"

#include <vector>
#include <string>

class Lexer {
private:
    Module            *module;
    DiagnosticsEngine *diagnostic_engine;

    /// Current position within the module's source buffer.
    size_t pos = 0;

    /// Consumes the current character, advances the cursor and updates the current location.
    inline int get() noexcept {
        int c = (pos < module->get_buffer().size()) ? module->get_buffer()[pos++] : EOF;

        if (c == '\n') {
            module->loc().line++;
            module->loc().column = 1;
        } else {
            module->loc().column++;
        }

        return c;
    }

    /// Returns the next char whitout consuming it.
    inline int peek() const noexcept {
        return (pos < module->get_buffer().size()) ? module->get_buffer()[pos] : EOF;
    }

public:
    Lexer(Module *mod, DiagnosticsEngine *diag_engine) : module(mod), diagnostic_engine(diag_engine) {}

    token_t read_identifier() noexcept;
    token_t read_string() noexcept;
    token_t read_number() noexcept;
    token_t read_operator() noexcept;

    inline bool is_symbol(const int) const noexcept;
    inline bool is_operator(const int) const noexcept;

    char read_escape(int) const noexcept;

    void read_octal_escape(std::string&) noexcept;
    void read_hexadecimal_escape(std::string&) noexcept;
    void read_unicode_escape(std::string&, const int) noexcept;

    void ignore_comment() noexcept;

    /// Main lexer entry point. Performs lexical analysis over the source module 
    /// and generates the complete token stream.
    void tokenize(std::vector<token_t> &tokens) noexcept;
    void print_tokens(Module&) noexcept;
};

const char *to_string_token(TokenType type);