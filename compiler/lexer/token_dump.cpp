/** @file token_dump.cpp
 * @brief Compact per-module token inspection with single-line escaped values.
 */
#include "compiler/lexer/lexer.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace optic::compiler::lexer {
namespace {
std::string escape_text(std::string_view value) {
    constexpr std::string_view hex_digits = "0123456789abcdef";
    std::string escaped;
    for (const unsigned char character : value) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\a':
                escaped += "\\a";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\v':
                escaped += "\\v";
                break;
            default:
                if (character < 0x20 || character == 0x7f) {
                    escaped += "\\x";
                    escaped += hex_digits[character >> 4];
                    escaped += hex_digits[character & 0x0f];
                } else {
                    escaped += static_cast<char>(character);
                }
        }
    }
    return escaped;
}

std::string display_path(const driver::Module& source) {
    const std::filesystem::path path(source.get_path());
    std::error_code error;
    const auto working_directory = std::filesystem::current_path(error);
    if (!error && path.is_absolute()) {
        const auto relative = path.lexically_relative(working_directory);
        if (!relative.empty()) {
            return escape_text(relative.generic_string());
        }
    }
    return escape_text(path.generic_string());
}

std::string position(const token_t& token) {
    return std::to_string(token.line) + ":" + std::to_string(token.column);
}

std::string representation(const token_t& token) {
    if (token.type == TokenType::EndOfFile) {
        return "—";
    }
    if (token.type == TokenType::Newline) {
        return "\"\\n\"";
    }
    return "\"" + escape_text(token.value) + "\"";
}
} // namespace

void Lexer::print_tokens(const driver::Module& source) {
    print_tokens(source, std::cout);
}

void Lexer::print_tokens(const driver::Module& source, std::ostream& out) {
    constexpr std::string_view position_header = "Line:Col";
    constexpr std::string_view type_header = "Type";
    std::size_t position_width = position_header.size();
    std::size_t type_width = type_header.size();
    for (const auto& token : source.get_tokens()) {
        position_width = std::max(position_width, position(token).size());
        type_width = std::max(type_width, std::string_view(to_string_token(token.type)).size());
    }

    out << "Tokens - " << display_path(source) << "\n\n"
        << "  " << std::string(position_width - position_header.size(), ' ') << position_header
        << "  " << type_header << std::string(type_width - type_header.size(), ' ')
        << "  Representation\n"
        << "  " << std::string(position_width, '-') << "  " << std::string(type_width, '-')
        << "  --------------\n";
    for (const auto& token : source.get_tokens()) {
        const auto location = position(token);
        const std::string_view type = to_string_token(token.type);
        out << "  " << std::string(position_width - location.size(), ' ') << location << "  "
            << type << std::string(type_width - type.size(), ' ') << "  " << representation(token)
            << '\n';
    }
    out << '\n';
}
} // namespace optic::compiler::lexer
