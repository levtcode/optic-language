#include "compiler/parser/parser.hpp"

namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;

bool Parser::is_builtin_type(TokenType t) {
    switch (t) {
        case Int:
        case Float:
        case Double:
        case Bool:
        case String:
        case Char:
        case Void:
        case Long:
        case Short:
        case Unsigned:
        case Array:
        case Vector:
        case Tuple:
        case Dict:
            return true;
        default:
            return false;
    }
}

ast::TypeRef Parser::parse_type_atom() {
    Depth guard(*this);
    if (soft_lines) {
        skip_newlines();
    }
    const auto start = cursor.peek();
    const bool constant = cursor.accept(Const);
    if (!is_builtin_type(cursor.peek().type) && !cursor.at(Identifier)) {
        fail("Expected a type name.");
    }
    const auto first = cursor.advance();
    std::string name = first.value;
    // Retain multi-word integer types as a single syntactic name.
    if (first.type == Unsigned || first.type == Long || first.type == Short) {
        while (cursor.at(Int) || cursor.at(Long) || cursor.at(Short)) {
            name += " " + cursor.advance().value;
        }
    }
    while (cursor.accept(Scope)) {
        name += "::" + expect(Identifier, "Expected a qualified type name.").value;
    }
    ast::TypeRef result(span(start), name);
    result.constant = constant;
    if (cursor.accept(LeftBracket)) {
        SoftLines lines(*this);
        skip_newlines();
        if (cursor.at(RightBracket)) {
            fail("Expected at least one type argument.");
        }
        do {
            skip_newlines();
            // Names are deliberately unresolved: a non-type argument may also be a name.
            if (cursor.at(IntegerLiteral) || cursor.at(Minus) || cursor.at(Plus) ||
                cursor.at(LeftParen)) {
                const auto value_start = cursor.peek();
                ast::TypeRef argument(span(value_start), "");
                argument.value_argument = parse_expression(2);
                argument.span = span(value_start);
                result.arguments.push_back(std::move(argument));
            } else {
                const auto saved = cursor.position();
                const auto saved_probe = probing;
                probing = true;
                std::optional<ast::TypeRef> candidate;
                try {
                    auto argument = parse_type();
                    skip_newlines();
                    if (cursor.at(Comma) || cursor.at(RightBracket)) {
                        candidate = std::move(argument);
                    }
                } catch (const ParseError&) {
                }
                probing = saved_probe;
                if (candidate) {
                    result.arguments.push_back(std::move(*candidate));
                } else {
                    cursor.restore(saved);
                    const auto value_start = cursor.peek();
                    ast::TypeRef argument(span(value_start), "");
                    argument.value_argument = parse_expression(2);
                    argument.span = span(value_start);
                    result.arguments.push_back(std::move(argument));
                }
            }
            skip_newlines();
            if (!cursor.accept(Comma)) {
                break;
            }
            skip_newlines();
            if (cursor.at(RightBracket)) {
                break;
            }
        } while (true);
        expect(RightBracket, "Expected ']' after type arguments.");
    }
    if (soft_lines) {
        skip_newlines();
    }
    while (cursor.accept(Star)) {
        ++result.pointers;
        if (soft_lines) {
            skip_newlines();
        }
    }
    if (cursor.accept(BitAnd)) {
        result.reference = true;
    }
    result.span = span(start);
    return result;
}

ast::TypeRef Parser::parse_type(bool union_allowed) {
    auto result = parse_type_atom();
    if (soft_lines) {
        skip_newlines();
    }
    if (union_allowed && cursor.at(BitOr)) {
        const auto first_span = result.span;
        ast::TypeRef union_type(first_span, "");
        union_type.alternatives.push_back(std::move(result));
        while (cursor.accept(BitOr)) {
            skip_newlines();
            union_type.alternatives.push_back(parse_type_atom());
        }
        union_type.span.end = cursor.previous().end_offset;
        return union_type;
    }
    return result;
}

bool Parser::is_declaration_ahead() {
    if (is_builtin_type(cursor.peek().type) || cursor.at(Const)) {
        return true;
    }
    if (!cursor.at(Identifier)) {
        return false;
    }
    const auto saved = cursor.position();
    const auto saved_probe = probing;
    probing = true;
    bool result = false;
    try {
        auto candidate = parse_type(false);
        if (cursor.accept(Identifier)) {
            const auto next = cursor.peek().type;
            result = next == Equal || next == Comma || next == Semicolon || next == Newline ||
                     next == EndOfFile || next == RightBrace;
        }
    } catch (const ParseError&) {
    }
    cursor.restore(saved);
    probing = saved_probe;
    return result;
}
} // namespace optic::compiler::parser
