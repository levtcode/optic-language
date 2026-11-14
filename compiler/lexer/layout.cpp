/** @file layout.cpp
 * @brief Preserve newline boundaries. The parser decides when expressions continue.
 */
#include "compiler/lexer/lexer.hpp"

namespace optic::compiler::lexer {
void Lexer::tokenize_layout(std::vector<token_t>& tokens) {
    std::vector<token_t> raw;
    tokenize(raw);
    tokens.clear();
    const auto source = module.get_buffer();
    std::size_t previous_end = 0;
    unsigned line = 1, column = 1;
    std::size_t scanned = 0;
    for (const auto& token : raw) {
        const auto gap = source.substr(previous_end, token.offset - previous_end);
        const auto newline = gap.find('\n');
        if (newline != std::string_view::npos) {
            const auto offset = previous_end + newline;
            while (scanned < offset) {
                if (source[scanned++] == '\n') { ++line; column = 1; } else ++column;
            }
            tokens.emplace_back(TokenType::Newline, "", line, column);
            tokens.back().offset = offset;
            tokens.back().end_offset = offset + 1;
        }
        tokens.push_back(token);
        previous_end = token.end_offset;
    }
}
}
