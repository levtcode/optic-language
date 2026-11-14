#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include <algorithm>

namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;

namespace {
int get_operator_precedence(TokenType token_type) {
    switch (token_type) {
        case Equal:
        case PlusAssign:
        case MinusAssign:
        case MulAssign:
        case DivAssign:
        case ModAssign:
        case AndAssign:
        case OrAssign:
        case LShiftAssign:
        case RShiftAssign:
            return 1;
        case Or:
        case Or_C:
            return 2;
        case And:
        case And_C:
            return 3;
        case BitOr:
            return 4;
        case BitXor:
            return 5;
        case BitAnd:
            return 6;
        case EqualEqual:
        case NotEqual:
            return 7;
        case Less:
        case LessEqual:
        case Greater:
        case GreaterEqual:
            return 8;
        case LeftShift:
        case RightShift:
            return 9;
        case Plus:
        case Minus:
            return 10;
        case Star:
        case Slash:
        case Mod:
            return 11;
        default:
            return 0;
    }
}

bool can_start_operand(TokenType token_type) {
    switch (token_type) {
        case Identifier:
        case IntegerLiteral:
        case FloatLiteral:
        case StringLiteral:
        case CharLiteral:
        case LiteralTrue:
        case LiteralFalse:
        case Null:
        case RawString:
        case FString:
        case LeftParen:
        case Plus:
        case Minus:
        case Star:
        case BitAnd:
        case Not:
        case Not_C:
        case BitNot:
        case New:
            return true;
        default:
            return false;
    }
}
} // namespace

bool Parser::is_assignment_operator(TokenType token_type) {
    return get_operator_precedence(token_type) == 1;
}

bool Parser::is_assignable(const ast::Expr& value) {
    if (dynamic_cast<const ast::NameExpr*>(&value) ||
        dynamic_cast<const ast::MemberExpr*>(&value) ||
        dynamic_cast<const ast::IndexExpr*>(&value) || dynamic_cast<const ast::CallExpr*>(&value)) {
        return true;
    }
    if (const auto* unary = dynamic_cast<const ast::UnaryExpr*>(&value)) {
        return unary->op == Star;
    }
    return false;
}

ast::ExprPtr Parser::parse_expression(int minimum_precedence) {
    const auto begin = cursor.position();
    const auto previous_ambiguity_count = ambiguous_casts;
    const bool outermost = expression_nesting++ == 0;

    struct Restore {
        unsigned& value;

        ~Restore() {
            --value;
        }
    } restore{expression_nesting};

    auto result = parse_binary_expression(minimum_precedence);
    if (outermost && ambiguous_casts != previous_ambiguity_count) {
        const auto tokens = cursor.slice(begin, cursor.position());
        const auto location = result->span;
        const auto height = result->height + 1;
        result =
            check_expression_height(std::make_unique<ast::UnresolvedExpr>(
                                        location,
                                        std::move(result),
                                        std::vector<lexer::token_t>(tokens.begin(), tokens.end())),
                                    height);
    }
    return result;
}

ast::ExprPtr Parser::parse_binary_expression(int minimum_precedence) {
    Depth guard(*this);
    if (soft_lines) {
        skip_newlines();
    }
    auto left = parse_unary_expression();
    while (true) {
        if (soft_lines) {
            skip_newlines();
        }
        const auto operator_precedence = get_operator_precedence(cursor.peek().type);
        if (operator_precedence < minimum_precedence) {
            break;
        }
        const auto operator_token = cursor.advance();
        skip_newlines();
        auto right =
            parse_expression(is_assignment_operator(operator_token.type) ? operator_precedence
                                                                         : operator_precedence + 1);
        auto location = left->span;
        location.end = right->span.end;
        const auto height = 1 + std::max(left->height, right->height);
        if (is_assignment_operator(operator_token.type)) {
            if (!is_assignable(*left)) {
                fail("Invalid assignment target.");
            }
            auto assign =
                std::make_unique<ast::AssignExpr>(location, std::move(left), std::move(right));
            assign->op = operator_token.type;
            left = check_expression_height(std::move(assign), height);
        } else {
            left = check_expression_height(
                std::make_unique<ast::BinaryExpr>(
                    location, operator_token.type, std::move(left), std::move(right)),
                height);
        }
    }
    return left;
}

ast::ExprPtr Parser::parse_unary_expression() {
    Depth guard(*this);
    if (soft_lines) {
        skip_newlines();
    }
    const auto start = cursor.peek();
    if (cursor.accept(Plus) || cursor.accept(Minus) || cursor.accept(Not) || cursor.accept(Not_C) ||
        cursor.accept(BitNot) || cursor.accept(BitAnd) || cursor.accept(Star) ||
        cursor.accept(Increment) || cursor.accept(Decrement) || cursor.accept(Delete)) {
        skip_newlines();
        auto operand = parse_unary_expression();
        if ((start.type == Increment || start.type == Decrement) && !is_assignable(*operand)) {
            fail("Invalid increment/decrement target.");
        }
        const auto height = 1 + operand->height;
        return check_expression_height(
            std::make_unique<ast::UnaryExpr>(span(start), start.type, std::move(operand)), height);
    }
    if (auto cast = try_parse_cast(start)) {
        return cast;
    }
    if (cursor.at(New)) {
        return parse_allocation(start);
    }
    return parse_postfix_expression(start);
}

ast::ExprPtr Parser::try_parse_cast(const lexer::token_t& start) {
    // C-style casts may depend on name resolution. Preserve ambiguous (T) -x forms
    // explicitly rather than assuming an unknown identifier is already a type.
    if (cursor.at(LeftParen)) {
        const auto saved_position = cursor.position();
        const auto was_probing = probing;
        probing = true;
        std::optional<ast::TypeRef> cast_type;
        bool is_known_type = false;
        try {
            cursor.advance();
            SoftLines lines(*this);
            skip_newlines();
            is_known_type = is_builtin_type(cursor.peek().type) || cursor.at(Const) ||
                            type_names.contains(cursor.peek().value);
            auto candidate = parse_type();
            skip_newlines();
            if (cursor.accept(RightParen) && can_start_operand(cursor.peek().type)) {
                cast_type = std::move(candidate);
            }
        } catch (const ParseError&) {
            // A failed lookahead is expected; normal parsing reports real errors.
        }
        probing = was_probing;
        if (cast_type) {
            const auto next = cursor.peek().type;
            auto value = parse_unary_expression();
            const auto height = 1 + value->height;
            auto cast = std::make_unique<ast::CastExpr>(
                span(start), std::move(*cast_type), std::move(value));
            cast->ambiguous =
                !is_known_type && (next == Plus || next == Minus || next == Star || next == BitAnd);
            if (cast->ambiguous) {
                ++ambiguous_casts;
            }
            return check_expression_height(std::move(cast), height);
        }
        cursor.restore(saved_position);
    }
    return nullptr;
}

ast::ExprPtr Parser::parse_allocation(const lexer::token_t& start) {
    expect(New, "Expected 'new'.");
    auto object_type = parse_type(false);
    auto result = std::make_unique<ast::NewExpr>(span(start), std::move(object_type));
    expect(LeftParen, "Expected '(' after the allocated type.");
    {
        SoftLines lines(*this);
        skip_newlines();
        while (!cursor.at(RightParen)) {
            result->arguments.push_back(parse_expression());
            skip_newlines();
            if (!cursor.accept(Comma)) {
                break;
            }
            skip_newlines();
        }
        expect(RightParen, "Expected ')' after construction arguments.");
    }
    unsigned height = 1;
    for (const auto& argument : result->arguments) {
        height = std::max(height, argument->height + 1);
    }
    result->span = span(start);
    return check_expression_height(std::move(result), height);
}

ast::ExprPtr Parser::parse_postfix_expression(const lexer::token_t& start) {
    auto result = parse_primary_expression();
    while (true) {
        if (soft_lines) {
            skip_newlines();
        }
        if (cursor.at(Point) || cursor.at(Scope)) {
            const bool qualified = cursor.advance().type == Scope;
            skip_newlines();
            const auto member = expect(Identifier, "Expected a member name.").value;
            const auto height = result->height + 1;
            auto access = std::make_unique<ast::MemberExpr>(span(start), std::move(result), member);
            access->qualified = qualified;
            result = check_expression_height(std::move(access), height);
        } else if (cursor.accept(LeftParen)) {
            auto call = std::make_unique<ast::CallExpr>(span(start), std::move(result));
            {
                SoftLines lines(*this);
                skip_newlines();
                while (!cursor.at(RightParen)) {
                    call->arguments.push_back(parse_expression());
                    skip_newlines();
                    if (!cursor.accept(Comma)) {
                        break;
                    }
                    skip_newlines();
                }
                expect(RightParen, "Expected ')' after call arguments.");
            }
            auto height = call->callee->height + 1;
            for (const auto& argument : call->arguments) {
                height = std::max(height, argument->height + 1);
            }
            call->span = span(start);
            result = check_expression_height(std::move(call), height);
        } else if (cursor.accept(LeftBracket)) {
            SoftLines lines(*this);
            skip_newlines();
            auto index = parse_expression();
            skip_newlines();
            expect(RightBracket, "Expected ']' after the index.");
            const auto height = 1 + std::max(result->height, index->height);
            result = check_expression_height(
                std::make_unique<ast::IndexExpr>(span(start), std::move(result), std::move(index)),
                height);
        } else if (cursor.at(Increment) || cursor.at(Decrement)) {
            const auto operator_token = cursor.advance().type;
            if (!is_assignable(*result)) {
                fail("Invalid increment/decrement target.");
            }
            const auto height = result->height + 1;
            result = check_expression_height(
                std::make_unique<ast::PostfixExpr>(span(start), operator_token, std::move(result)),
                height);
        } else {
            break;
        }
    }
    return result;
}

ast::ExprPtr Parser::parse_collection(TokenType close, ast::CollectionExpr::Kind kind) {
    Depth guard(*this);
    const auto start = cursor.previous();
    auto result = std::make_unique<ast::CollectionExpr>(span(start), kind);
    SoftLines lines(*this);
    skip_newlines();
    if (cursor.accept(close)) {
        result->span = span(start);
        return result;
    }
    result->elements.push_back(parse_expression());
    skip_newlines();
    const bool dictionary = close == RightBrace && cursor.accept(Colon);
    if (dictionary) {
        result->kind = ast::CollectionExpr::Kind::Dictionary;
        skip_newlines();
        result->elements.push_back(parse_expression());
    } else if (close == RightBrace) {
        result->kind = ast::CollectionExpr::Kind::Array;
    }
    skip_newlines();
    const bool comma = cursor.accept(Comma);
    if (close == RightParen && !comma) {
        expect(RightParen, "Expected ')' after the expression.");
        auto value = std::move(result->elements.front());
        value->span = span(start);
        return value;
    }
    if (comma) {
        skip_newlines();
        while (!cursor.at(close)) {
            result->elements.push_back(parse_expression());
            skip_newlines();
            if (dictionary) {
                expect(Colon, "Expected ':' between dictionary key and value.");
                skip_newlines();
                result->elements.push_back(parse_expression());
            }
            skip_newlines();
            if (!cursor.accept(Comma)) {
                break;
            }
            skip_newlines();
        }
    }
    expect(close, "Expected the matching collection delimiter.");
    unsigned height = 1;
    for (const auto& element : result->elements) {
        height = std::max(height, element->height + 1);
    }
    result->span = span(start);
    return check_expression_height(std::move(result), height);
}

ast::ExprPtr Parser::parse_primary_expression() {
    const auto start = cursor.peek();
    if (cursor.accept(Identifier)) {
        return std::make_unique<ast::NameExpr>(span(start), start.value);
    }
    if (cursor.accept(LeftParen)) {
        return parse_collection(RightParen, ast::CollectionExpr::Kind::Tuple);
    }
    if (cursor.accept(LeftBracket)) {
        return parse_collection(RightBracket, ast::CollectionExpr::Kind::Vector);
    }
    if (cursor.accept(LeftBrace)) {
        return parse_collection(RightBrace, ast::CollectionExpr::Kind::EmptyBraces);
    }
    if (cursor.at(FString)) {
        return parse_formatted_string();
    }
    if (cursor.at(StringLiteral) || cursor.at(RawString)) {
        std::string value;
        while (true) {
            if (cursor.accept(RawString)) {
            }
            value += expect(StringLiteral, "Expected a string literal.").value;
            const auto saved_position = cursor.position();
            skip_newlines();
            if (!cursor.at(StringLiteral) && !cursor.at(RawString)) {
                cursor.restore(saved_position);
                break;
            }
        }
        return std::make_unique<ast::LiteralExpr>(span(start), StringLiteral, value);
    }
    switch (start.type) {
        case IntegerLiteral:
        case FloatLiteral:
        case CharLiteral:
        case LiteralTrue:
        case LiteralFalse:
        case Null:
            cursor.advance();
            return std::make_unique<ast::LiteralExpr>(span(start), start.type, start.value);
        default:
            fail(std::string("Expected an expression; found ") +
                 lexer::to_string_token(start.type) + ".");
    }
}
} // namespace optic::compiler::parser
