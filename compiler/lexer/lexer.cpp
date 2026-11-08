/** @file lexer.cpp
 * @brief Token recognition without cursor rollback or loss of source positions.
 */
#include "compiler/lexer/lexer.hpp"
#include "compiler/lexer/tables.hpp"
#include <cctype>
#include <cstdio>
#include <iostream>

namespace optic::compiler::lexer {
namespace {
bool alpha(int c) { return c != EOF && (std::isalpha(c) || c == '_'); }
bool digit(int c) { return c >= '0' && c <= '9'; }
int hex(int c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
void utf8(std::string& value, unsigned code) {
    if (code <= 0x7f) value += static_cast<char>(code);
    else if (code <= 0x7ff) {
        value += static_cast<char>(0xc0 | (code >> 6));
        value += static_cast<char>(0x80 | (code & 0x3f));
    } else if (code <= 0xffff) {
        value += static_cast<char>(0xe0 | (code >> 12));
        value += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        value += static_cast<char>(0x80 | (code & 0x3f));
    } else {
        value += static_cast<char>(0xf0 | (code >> 18));
        value += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
        value += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        value += static_cast<char>(0x80 | (code & 0x3f));
    }
}
}
int Lexer::peek(std::size_t offset) const noexcept {
    const auto source = module.get_buffer();
    return offset < source.size() - pos ? static_cast<unsigned char>(source[pos + offset]) : EOF;
}
int Lexer::get() noexcept {
    const int c = peek();
    if (c == EOF) return c;
    ++pos;
    if (c == '\n') { ++location.line; location.column = 1; }
    else ++location.column;
    return c;
}
void Lexer::error(const driver::SourceLocation& loc, const std::string& message) {
    diagnostic_engine.report(loc, "Error: " + message, "", "", driver::DiagnosticsLevel::Error);
}
token_t Lexer::read_identifier() {
    const auto loc = location;
    std::string word;
    while (alpha(peek()) || digit(peek())) word += static_cast<char>(get());
    const auto it = keyword_table.find(word);
    return {it == keyword_table.end() ? TokenType::Identifier : it->second, word, loc.line, loc.column};
}
token_t Lexer::read_number() {
    const auto loc = location;
    std::string value;
    bool floating = false;
    while (digit(peek())) value += static_cast<char>(get());
    if (peek() == '.' && digit(peek(1))) {
        floating = true;
        value += static_cast<char>(get());
        while (digit(peek())) value += static_cast<char>(get());
    }
    if (peek() == 'e' || peek() == 'E') {
        const auto offset = (peek(1) == '+' || peek(1) == '-') ? 2u : 1u;
        if (digit(peek(offset))) {
            floating = true;
            value += static_cast<char>(get());
            if (peek() == '+' || peek() == '-') value += static_cast<char>(get());
            while (digit(peek())) value += static_cast<char>(get());
        }
    }
    return {floating ? TokenType::FloatLiteral : TokenType::IntegerLiteral, value, loc.line, loc.column};
}
token_t Lexer::read_string(bool raw) {
    const auto loc = location;
    const auto quote = get();
    std::string value;
    while (peek() != EOF && peek() != quote) {
        if (peek() == '\n' || peek() == '\r') {
            error(loc, "String not closed before end of line.");
            return {quote == '\'' ? TokenType::CharLiteral : TokenType::StringLiteral, value, loc.line, loc.column};
        }
        int c = get();
        if (c != '\\' || raw) { value += static_cast<char>(c); continue; }
        const auto escape_loc = location;
        if (peek() == '\n' || peek() == '\r') {
            error(loc, "String not closed before end of line.");
            return {quote == '\'' ? TokenType::CharLiteral : TokenType::StringLiteral, value, loc.line, loc.column};
        }
        c = get();
        if (c == EOF) break;
        switch (c) {
            case 'a': value += '\a'; break;
            case 'b': value += '\b'; break;
            case 't': value += '\t'; break;
            case 'n': value += '\n'; break;
            case 'v': value += '\v'; break;
            case 'f': value += '\f'; break;
            case 'r': value += '\r'; break;
            case '\\': value += '\\'; break;
            case '\'': value += '\''; break;
            case '"': value += '"'; break;
            case 'x': case 'u': case 'U': {
                const unsigned width = c == 'x' ? 2 : c == 'u' ? 4 : 8;
                unsigned code = 0, count = 0;
                while (count < width && hex(peek()) >= 0) {
                    code = code * 16 + hex(get());
                    ++count;
                }
                if ((c == 'x' && count == 0) || (c != 'x' && count != width)) {
                    error(escape_loc, "Invalid hexadecimal or Unicode escape.");
                } else if (c == 'x') value += static_cast<char>(code);
                else if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                    error(escape_loc, "Invalid Unicode scalar value.");
                else utf8(value, code);
                break;
            }
            default:
                if (c >= '0' && c <= '7') {
                    unsigned code = c - '0';
                    for (int n = 1; n < 3 && peek() >= '0' && peek() <= '7'; ++n)
                        code = code * 8 + get() - '0';
                    if (code > 255) error(escape_loc, "Octal escape exceeds one byte.");
                    else value += static_cast<char>(code);
                } else error(escape_loc, "Unknown string escape.");
        }
    }
    if (peek() == quote) get();
    else error(loc, "String not closed.");
    return {quote == '\'' ? TokenType::CharLiteral : TokenType::StringLiteral, value, loc.line, loc.column};
}
token_t Lexer::read_operator() {
    const auto loc = location;
    const auto remaining = module.get_buffer().substr(pos);
    std::string_view longest;
    TokenType type = TokenType::EndOfFile;
    for (const auto& [spelling, candidate] : operator_table) {
        if (spelling.size() > longest.size() && remaining.starts_with(spelling)) {
            longest = spelling;
            type = candidate;
        }
    }
    if (longest.empty()) {
        error(loc, "Unrecognized operator.");
        return {TokenType::EndOfFile, std::string(1, static_cast<char>(get())), loc.line, loc.column};
    }
    for (std::size_t i = 0; i < longest.size(); ++i) get();
    return {type, std::string(longest), loc.line, loc.column};
}
void Lexer::ignore_comment() {
    const auto loc = location;
    get();
    const int kind = get();
    if (kind == '/') {
        while (peek() != EOF && peek() != '\n') get();
        return;
    }
    while (peek() != EOF) {
        if (get() == '*' && peek() == '/') { get(); return; }
    }
    error(loc, "Block comment not closed, '*/' expected.");
}
void Lexer::tokenize(std::vector<token_t>& tokens) {
    pos = 0;
    location = {driver::SourceKind::BufferNoAST, module.get_path(), "", 1, 1};
    tokens.clear();
    while (peek() != EOF) {
        const auto loc = location;
        const auto c = peek();
        if (std::isspace(c)) { get(); continue; }
        if (c == '/' && (peek(1) == '/' || peek(1) == '*')) { ignore_comment(); continue; }
        if ((c == 'r' || c == 'f') && peek(1) == '"') {
            get();
            tokens.emplace_back(c == 'r' ? TokenType::RawString : TokenType::FString,
                                std::string(1, static_cast<char>(c)), loc.line, loc.column);
            tokens.push_back(read_string(c == 'r'));
        } else if (alpha(c)) tokens.push_back(read_identifier());
        else if (digit(c)) tokens.push_back(read_number());
        else if (c == '"' || c == '\'') tokens.push_back(read_string());
        else if (const auto it = symbol_table.find(c); it != symbol_table.end()) {
            tokens.emplace_back(it->second, std::string(1, static_cast<char>(get())), loc.line, loc.column);
        } else if (std::string_view("=+-*/%&|~^<>!").find(static_cast<char>(c)) != std::string_view::npos)
            tokens.push_back(read_operator());
        else { error(loc, "Unexpected character in source."); get(); }
    }
    tokens.emplace_back(TokenType::EndOfFile, "EOF", location.line, location.column);
}
const char* to_string_token(TokenType type) {
    switch (type) {
#define X(name) case TokenType::name: return #name;
#include "tokens_types.def"
#undef X
    }
    return "Unknown";
}
void Lexer::print_tokens(const driver::Module& source) {
    for (const auto& token : source.get_tokens()) {
        std::cout << "Token type: " << to_string_token(token.type) << "\n"
                  << "Token value: '" << token.value << "'\n"
                  << "At line: " << token.line << ", in column: " << token.column << "\n"
                  << "In file: " << source.get_path() << "\n\n";
    }
}
} // namespace optic::compiler::lexer
