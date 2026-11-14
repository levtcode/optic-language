/** @file lexer.hpp
 * @brief Location-preserving lexical analysis of one source module.
 */
#pragma once

#include "compiler/driver/module.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace optic::compiler::lexer {
class Lexer {
public:
    Lexer(driver::Module& mod, driver::DiagnosticsEngine& diagnostics)
        : module(mod),
          diagnostic_engine(diagnostics) {}

    void tokenize(std::vector<token_t>& tokens);
    /// Parser stream, including physical newline boundaries; indentation has no syntax.
    void tokenize_layout(std::vector<token_t>& tokens);
    static void print_tokens(const driver::Module& module);
    static void print_tokens(const driver::Module& module, std::ostream& out);

private:
    driver::Module& module;
    driver::DiagnosticsEngine& diagnostic_engine;
    driver::SourceLocation location;
    std::size_t pos = 0;
    int peek(std::size_t offset = 0) const noexcept;
    int get() noexcept;
    token_t read_identifier();
    token_t read_number();
    token_t read_string(bool raw = false, bool formatted = false);
    token_t read_operator();
    void ignore_comment();
    void error(const driver::SourceLocation& loc, const std::string& message);
};

const char* to_string_token(TokenType type);
} // namespace optic::compiler::lexer
