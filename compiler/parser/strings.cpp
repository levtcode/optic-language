#include "compiler/parser/parser.hpp"
#include "compiler/lexer/lexer.hpp"
#include <algorithm>
namespace optic::compiler::parser {
using enum lexer::TokenType;
ast::ExprPtr Parser::fragment_expression(const std::string& source, const lexer::token_t& origin) {
    driver::Module fragment(module.get_path());
    driver::DiagnosticsEngine lexical_errors;
    fragment.set_buffer(source);
    lexer::Lexer(fragment, lexical_errors).tokenize_layout(fragment.get_tokens());
    if (lexical_errors.has_errors()) fail("Invalid lexical input inside formatted-string expression.");
    for (auto& token : fragment.get_tokens()) {
        token.offset += origin.offset; token.end_offset += origin.offset;
        if (token.line == 1) token.column += origin.column - 1;
        token.line += origin.line - 1;
    }
    // Borrow the original module for nested literal spans; no copy of its source.
    Parser parser(id, module, diagnostics, fragment.get_tokens());
    parser.depth = depth;
    parser.type_names = type_names;
    parser.soft_lines = 1;
    try {
        parser.newlines();
        auto value = parser.expression(); parser.newlines();
        parser.expect(EndOfFile, "Unexpected token inside formatted-string expression.");
        return value;
    } catch (const ParseError&) { fail("Invalid formatted-string expression."); }
      catch (const LimitError&) { fail("Formatted-string expression exceeds parser limits."); }
}
ast::ExprPtr Parser::formatted() {
    Depth guard(*this);
    const auto start = cursor.advance();
    const auto quoted = expect(StringLiteral, "Expected a formatted string body.");
    const auto source = module.get_buffer();
    const auto body_start = quoted.offset + 1;
    const auto body = source.substr(body_start, quoted.end_offset - body_start - 1);
    auto result = std::make_unique<ast::FormattedExpr>(span(start));
    auto origin = lexer::token_t(StringLiteral, "", quoted.line, quoted.column + 1);
    std::size_t scanned = 0;
    auto origin_at = [&](std::size_t index) {
        while (scanned < index) {
            if (body[scanned++] == '\n') { ++origin.line; origin.column = 1; } else ++origin.column;
        }
        origin.offset = origin.end_offset = body_start + index;
        return origin;
    };
    std::string literal;
    std::size_t literal_start = 0;
    auto flush = [&](std::size_t end) {
        if (literal.empty()) return;
        driver::Module segment(module.get_path());
        driver::DiagnosticsEngine lexical_errors;
        segment.set_buffer("\"" + literal + "\"");
        lexer::Lexer(segment, lexical_errors).tokenize(segment.get_tokens());
        if (lexical_errors.has_errors()) fail("Invalid formatted-string literal segment.");
        const auto origin = origin_at(literal_start);
        ast::SourceSpan location{id, origin.offset, body_start + end, origin.line, origin.column};
        result->parts.push_back(std::make_unique<ast::LiteralExpr>(location, StringLiteral, segment.get_tokens()[0].value));
        literal.clear();
    };
    for (std::size_t i = 0; i < body.size();) {
        if (literal.empty()) literal_start = i;
        if (body[i] == '\\' && i + 1 < body.size()) {
            literal += body[i++]; literal += body[i++]; continue;
        }
        if ((body[i] == '{' || body[i] == '}') && i + 1 < body.size() && body[i+1] == body[i]) {
            literal += body[i]; i += 2; continue;
        }
        if (body[i] == '}') fail("Unmatched '}' in formatted string.");
        if (body[i] != '{') { literal += body[i++]; continue; }
        flush(i);
        const auto begin = ++i;
        unsigned braces = 1;
        char quote = 0;
        while (i < body.size() && braces != 0) {
            const auto c = body[i];
            if (quote != 0) {
                if (c == '\\' && i + 1 < body.size()) { i += 2; continue; }
                if (c == quote) quote = 0;
            } else if (c == '\'' || c == '"') quote = c;
            else if (c == '{') ++braces;
            else if (c == '}') --braces;
            if (braces != 0) ++i;
        }
        if (braces != 0) fail("Unclosed formatted-string expression.");
        result->parts.push_back(fragment_expression(std::string(body.substr(begin, i - begin)), origin_at(begin)));
        ++i;
    }
    flush(body.size());
    unsigned height = 1;
    for (const auto& part : result->parts) height = std::max(height, part->height + 1);
    return bounded(std::move(result), height);
}
}
