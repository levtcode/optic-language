#pragma once
#include "compiler/lexer/tokens.hpp"
#include <algorithm>
#include <span>
namespace optic::compiler::parser {
/// Borrows an immutable stream ending in EOF. Parser validates this precondition.
class TokenCursor {
public:
    explicit TokenCursor(std::span<const lexer::token_t> tokens) : tokens(tokens) {}
    const lexer::token_t& peek(std::size_t ahead = 0) const {
        return tokens[std::min(index + ahead, tokens.size() - 1)];
    }
    const lexer::token_t& previous() const { return tokens[index == 0 ? 0 : index - 1]; }
    const lexer::token_t& advance() {
        const auto& token = peek();
        if (token.type != lexer::TokenType::EndOfFile) ++index;
        return token;
    }
    bool at(lexer::TokenType type) const { return peek().type == type; }
    bool accept(lexer::TokenType type) { if (!at(type)) return false; advance(); return true; }
    std::size_t position() const { return index; }
    std::span<const lexer::token_t> slice(std::size_t begin, std::size_t end) const { return tokens.subspan(begin, end - begin); }
    void restore(std::size_t position) { index = position; }
private:
    std::span<const lexer::token_t> tokens;
    std::size_t index = 0;
};
}
