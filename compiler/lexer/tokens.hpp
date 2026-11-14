/**
 * @file tokens.hpp
 * @brief Token kinds and source-positioned lexical tokens.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::lexer.
 */

#pragma once

#include <string>
#include <cstddef>

namespace optic::compiler::lexer {

/// Keep token kinds and their debug names in one definition list.
enum class TokenType {
#define X(name) name,
#include "tokens_types.def"
#undef X
};

struct token_t {
    TokenType type;
    std::string value;
    unsigned line;
    unsigned column;
    // Half-open byte range in the original source, independent of decoded value.
    std::size_t offset = 0;
    std::size_t end_offset = 0;
    
    token_t(const TokenType typ, const std::string &val, const unsigned ln, const unsigned col) : 
    type(typ), value(val), line(ln), column(col) {}
};

} // namespace optic::compiler::lexer
