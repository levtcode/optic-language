/** @file layout.cpp
 * @brief Normalize physical lines into logical separators and colon block boundaries.
 */
#include "compiler/lexer/lexer.hpp"

namespace optic::compiler::lexer {
namespace {
bool continues(TokenType t) {
    using enum TokenType;
    switch (t) {
        case Equal: case Plus: case Minus: case Star: case Slash: case Mod:
        case EqualEqual: case NotEqual: case Less: case LessEqual: case Greater:
        case GreaterEqual: case And: case Or: case And_C: case Or_C:
        case Not: case Not_C: case Comma: case Arrow: return true;
        default: return false;
    }
}
}
void Lexer::tokenize_layout(std::vector<token_t>& tokens) {
    std::vector<token_t> raw;
    tokenize(raw);
    tokens.clear();
    struct Level { unsigned body, header, braces; };
    std::vector<Level> levels;
    unsigned braces = 0, parens = 0, brackets = 0;
    unsigned previous_line = 0, previous_indent = 0;
    TokenType previous = TokenType::EndOfFile;
    const auto source = module.get_buffer();
    auto marker = [&](TokenType kind, const token_t& at) {
        tokens.emplace_back(kind, "", at.line, at.column);
        tokens.back().offset = tokens.back().end_offset = at.offset;
    };
    auto issue = [&](const token_t& at, const std::string& message) {
        error({driver::SourceKind::BufferNoAST, module.get_path(), "", at.line, at.column}, message);
    };
    for (const auto& token : raw) {
        const bool eof = token.type == TokenType::EndOfFile;
        const bool new_line = previous_line != token.line;
        if (eof) {
            if (previous != TokenType::EndOfFile) marker(TokenType::Newline, token);
            if (previous == TokenType::Colon) issue(token, "Expected an indented block after ':'.");
            while (!levels.empty()) { marker(TokenType::Dedent, token); levels.pop_back(); }
            tokens.push_back(token);
            break;
        }
        if (new_line) {
            const auto start = token.offset == 0 ? std::string_view::npos : source.rfind('\n', token.offset - 1);
            auto cursor = start == std::string_view::npos ? 0 : start + 1;
            unsigned indent = 0;
            while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t')) {
                if (source[cursor] == '\t') issue(token, "Use spaces, not tabs, for leading indentation.");
                ++indent; ++cursor;
            }
            const bool continuation = parens != 0 || brackets != 0 || continues(previous);
            if (previous_line != 0 && parens == 0 && brackets == 0) marker(TokenType::Newline, token);
            if (!continuation) {
                if (previous == TokenType::Colon) {
                    if (indent <= previous_indent || token.type == TokenType::RightBrace)
                        issue(token, "Expected an indented block after ':'.");
                    else {
                        levels.push_back({indent, previous_indent, braces});
                        marker(TokenType::Indent, token);
                    }
                } else if (token.type != TokenType::RightBrace) {
                    bool popped = false;
                    unsigned header = 0;
                    while (!levels.empty() && levels.back().braces == braces && indent < levels.back().body) {
                        header = levels.back().header; popped = true;
                        marker(TokenType::Dedent, token); levels.pop_back();
                    }
                    if (popped && indent != header)
                        issue(token, "Dedent does not match an enclosing indentation level.");
                    if (!levels.empty() && levels.back().braces == braces && indent > levels.back().body)
                        issue(token, "Unexpected indentation; expected a block introduced by ':'.");
                    if (levels.empty() && braces == 0 && indent != 0)
                        issue(token, "Unexpected indentation at module scope.");
                }
            }
            // A continued header keeps the indentation of its first physical line.
            if (!continuation) previous_indent = indent;
        }
        if (token.type == TokenType::RightBrace) {
            while (!levels.empty() && levels.back().braces == braces) {
                marker(TokenType::Dedent, token); levels.pop_back();
            }
            if (braces != 0) --braces;
        }
        tokens.push_back(token);
        if (token.type == TokenType::LeftBrace) ++braces;
        if (token.type == TokenType::LeftParen) ++parens;
        if (token.type == TokenType::RightParen && parens != 0) --parens;
        if (token.type == TokenType::LeftBracket) ++brackets;
        if (token.type == TokenType::RightBracket && brackets != 0) --brackets;
        previous = token.type;
        previous_line = token.line;
    }
}
} // namespace optic::compiler::lexer
