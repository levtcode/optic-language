#include "compiler/parser/parser.hpp"
#include "compiler/lexer/lexer.hpp"
#include <algorithm>
namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;
namespace {
int precedence(TokenType t) {
    switch (t) {
        case Equal: case PlusAssign: case MinusAssign: case MulAssign: case DivAssign:
        case ModAssign: case AndAssign: case OrAssign: case LShiftAssign: case RShiftAssign: return 1;
        case Or: case Or_C: return 2;
        case And: case And_C: return 3;
        case BitOr: return 4;
        case BitXor: return 5;
        case BitAnd: return 6;
        case EqualEqual: case NotEqual: return 7;
        case Less: case LessEqual: case Greater: case GreaterEqual: return 8;
        case LeftShift: case RightShift: return 9;
        case Plus: case Minus: return 10;
        case Star: case Slash: case Mod: return 11;
        default: return 0;
    }
}
bool operand_start(TokenType t) {
    switch (t) {
        case Identifier: case IntegerLiteral: case FloatLiteral: case StringLiteral:
        case CharLiteral: case LiteralTrue: case LiteralFalse: case Null: case RawString:
        case FString: case LeftParen: case Plus: case Minus: case Star: case BitAnd:
        case Not: case Not_C: case BitNot: case New: return true;
        default: return false;
    }
}
}
bool Parser::assignment(TokenType t) { return precedence(t) == 1; }
bool Parser::assignable(const ast::Expr& value) {
    if (dynamic_cast<const ast::NameExpr*>(&value) || dynamic_cast<const ast::MemberExpr*>(&value) ||
        dynamic_cast<const ast::IndexExpr*>(&value) || dynamic_cast<const ast::CallExpr*>(&value)) return true;
    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&value)) return unary->op == Star;
    return false;
}
ast::ExprPtr Parser::expression(int minimum) {
    const auto begin = cursor.position();
    const auto before = ambiguous_casts;
    const bool outermost = expression_nesting++ == 0;
    struct Restore { unsigned& value; ~Restore() { --value; } } restore{expression_nesting};
    auto result = expression_impl(minimum);
    if (outermost && ambiguous_casts != before) {
        const auto tokens = cursor.slice(begin, cursor.position());
        const auto location = result->span;
        const auto height = result->height + 1;
        result = bounded(std::make_unique<ast::UnresolvedExpr>(location, std::move(result),
            std::vector<lexer::token_t>(tokens.begin(), tokens.end())), height);
    }
    return result;
}
ast::ExprPtr Parser::expression_impl(int minimum) {
    Depth guard(*this);
    if (soft_lines) newlines();
    auto left = unary();
    while (true) {
        if (soft_lines) newlines();
        const auto p = precedence(cursor.peek().type);
        if (p < minimum) break;
        const auto op = cursor.advance(); newlines();
        auto right = expression(assignment(op.type) ? p : p + 1);
        auto location = left->span; location.end = right->span.end;
        const auto height = 1 + std::max(left->height, right->height);
        if (assignment(op.type)) {
            if (!assignable(*left)) fail("Invalid assignment target.");
            auto assign = std::make_unique<ast::AssignExpr>(location, std::move(left), std::move(right));
            assign->op = op.type;
            left = bounded(std::move(assign), height);
        } else left = bounded(std::make_unique<ast::BinaryExpr>(location, op.type, std::move(left), std::move(right)), height);
    }
    return left;
}
ast::ExprPtr Parser::unary() {
    Depth guard(*this);
    if (soft_lines) newlines();
    const auto start = cursor.peek();
    if (cursor.accept(Plus) || cursor.accept(Minus) || cursor.accept(Not) || cursor.accept(Not_C) ||
        cursor.accept(BitNot) || cursor.accept(BitAnd) || cursor.accept(Star) ||
        cursor.accept(Increment) || cursor.accept(Decrement) || cursor.accept(Delete)) {
        newlines(); auto operand = unary();
        if ((start.type == Increment || start.type == Decrement) && !assignable(*operand)) fail("Invalid increment/decrement target.");
        const auto height = 1 + operand->height;
        return bounded(std::make_unique<ast::UnaryExpr>(span(start), start.type, std::move(operand)), height);
    }
    // C-style casts may depend on name resolution. Preserve ambiguous (T) -x forms
    // explicitly rather than assuming an unknown identifier is already a type.
    if (cursor.at(LeftParen)) {
        const auto saved = cursor.position();
        const auto saved_probe = probing;
        probing = true;
        std::optional<ast::TypeRef> cast_type;
        bool known = false;
        try {
            cursor.advance(); SoftLines lines(*this); newlines();
            known = basic_type(cursor.peek().type) || cursor.at(Const) || type_names.contains(cursor.peek().value);
            auto candidate = type(); newlines();
            if (cursor.accept(RightParen) && operand_start(cursor.peek().type)) cast_type = std::move(candidate);
        } catch (const ParseError&) {}
        probing = saved_probe;
        if (cast_type) {
            const auto next = cursor.peek().type;
            auto value = unary(); const auto height = 1 + value->height;
            auto cast = std::make_unique<ast::CastExpr>(span(start), std::move(*cast_type), std::move(value));
            cast->ambiguous = !known && (next == Plus || next == Minus || next == Star || next == BitAnd);
            if (cast->ambiguous) ++ambiguous_casts;
            return bounded(std::move(cast), height);
        }
        cursor.restore(saved);
    }
    if (cursor.accept(New)) {
        auto object_type = type(false);
        auto result = std::make_unique<ast::NewExpr>(span(start), std::move(object_type));
        expect(LeftParen, "Expected '(' after the allocated type.");
        {
            SoftLines lines(*this); newlines();
            while (!cursor.at(RightParen)) {
                result->arguments.push_back(expression()); newlines();
                if (!cursor.accept(Comma)) break;
                newlines();
            }
            expect(RightParen, "Expected ')' after construction arguments.");
        }
        unsigned height = 1;
        for (const auto& argument : result->arguments) height = std::max(height, argument->height + 1);
        result->span = span(start); return bounded(std::move(result), height);
    }
    auto result = primary();
    while (true) {
        if (soft_lines) newlines();
        if (cursor.at(Point) || cursor.at(Scope)) {
            const bool qualified = cursor.advance().type == Scope;
            newlines();
            const auto member = expect(Identifier, "Expected a member name.").value;
            const auto height = result->height + 1;
            auto access = std::make_unique<ast::MemberExpr>(span(start), std::move(result), member);
            access->qualified = qualified;
            result = bounded(std::move(access), height);
        } else if (cursor.accept(LeftParen)) {
            auto call = std::make_unique<ast::CallExpr>(span(start), std::move(result));
            {
                SoftLines lines(*this); newlines();
                while (!cursor.at(RightParen)) {
                    call->arguments.push_back(expression()); newlines();
                    if (!cursor.accept(Comma)) break;
                    newlines();
                }
                expect(RightParen, "Expected ')' after call arguments.");
            }
            auto height = call->callee->height + 1;
            for (const auto& argument : call->arguments) height = std::max(height, argument->height + 1);
            call->span = span(start); result = bounded(std::move(call), height);
        } else if (cursor.accept(LeftBracket)) {
            SoftLines lines(*this); newlines();
            auto index = expression(); newlines();
            expect(RightBracket, "Expected ']' after the index.");
            const auto height = 1 + std::max(result->height, index->height);
            result = bounded(std::make_unique<ast::IndexExpr>(span(start), std::move(result), std::move(index)), height);
        } else if (cursor.at(Increment) || cursor.at(Decrement)) {
            const auto op = cursor.advance().type;
            if (!assignable(*result)) fail("Invalid increment/decrement target.");
            const auto height = result->height + 1;
            result = bounded(std::make_unique<ast::PostfixExpr>(span(start), op, std::move(result)), height);
        } else break;
    }
    return result;
}
ast::ExprPtr Parser::collection(TokenType close, ast::CollectionExpr::Kind kind) {
    Depth guard(*this);
    const auto start = cursor.previous();
    auto result = std::make_unique<ast::CollectionExpr>(span(start), kind);
    SoftLines lines(*this); newlines();
    if (cursor.accept(close)) { result->span = span(start); return result; }
    result->elements.push_back(expression()); newlines();
    const bool dictionary = close == RightBrace && cursor.accept(Colon);
    if (dictionary) { result->kind = ast::CollectionExpr::Kind::Dictionary; newlines(); result->elements.push_back(expression()); }
    else if (close == RightBrace) result->kind = ast::CollectionExpr::Kind::Array;
    newlines();
    const bool comma = cursor.accept(Comma);
    if (close == RightParen && !comma) {
        expect(RightParen, "Expected ')' after the expression.");
        auto value = std::move(result->elements.front()); value->span = span(start); return value;
    }
    if (comma) {
        newlines();
        while (!cursor.at(close)) {
            result->elements.push_back(expression()); newlines();
            if (dictionary) { expect(Colon, "Expected ':' between dictionary key and value."); newlines(); result->elements.push_back(expression()); }
            newlines();
            if (!cursor.accept(Comma)) break;
            newlines();
        }
    }
    expect(close, "Expected the matching collection delimiter.");
    unsigned height = 1;
    for (const auto& element : result->elements) height = std::max(height, element->height + 1);
    result->span = span(start); return bounded(std::move(result), height);
}
ast::ExprPtr Parser::primary() {
    const auto start = cursor.peek();
    if (cursor.accept(Identifier)) return std::make_unique<ast::NameExpr>(span(start), start.value);
    if (cursor.accept(LeftParen)) return collection(RightParen, ast::CollectionExpr::Kind::Tuple);
    if (cursor.accept(LeftBracket)) return collection(RightBracket, ast::CollectionExpr::Kind::Vector);
    if (cursor.accept(LeftBrace)) return collection(RightBrace, ast::CollectionExpr::Kind::EmptyBraces);
    if (cursor.at(FString)) return formatted();
    if (cursor.at(StringLiteral) || cursor.at(RawString)) {
        std::string value;
        while (true) {
            if (cursor.accept(RawString)) {}
            value += expect(StringLiteral, "Expected a string literal.").value;
            const auto saved = cursor.position(); newlines();
            if (!cursor.at(StringLiteral) && !cursor.at(RawString)) { cursor.restore(saved); break; }
        }
        return std::make_unique<ast::LiteralExpr>(span(start), StringLiteral, value);
    }
    switch (start.type) {
        case IntegerLiteral: case FloatLiteral: case CharLiteral: case LiteralTrue: case LiteralFalse: case Null:
            cursor.advance(); return std::make_unique<ast::LiteralExpr>(span(start), start.type, start.value);
        default: fail(std::string("Expected an expression; found ") + lexer::to_string_token(start.type) + ".");
    }
}
}
