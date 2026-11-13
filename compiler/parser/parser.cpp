#include "compiler/parser/parser.hpp"
#include "compiler/lexer/lexer.hpp"
#include <algorithm>

namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;
namespace {
bool basic_type(TokenType t) { return t == Int || t == Float || t == Bool || t == String || t == Char; }
int precedence(TokenType t) {
    switch (t) {
        case Equal: return 1;
        case Or: case Or_C: return 2;
        case And: case And_C: return 3;
        case EqualEqual: case NotEqual: return 4;
        case Less: case LessEqual: case Greater: case GreaterEqual: return 5;
        case Plus: case Minus: return 6;
        case Star: case Slash: case Mod: return 7;
        default: return 0;
    }
}
}
Parser::Depth::Depth(Parser& parser) : parser(parser) {
    if (parser.depth >= 256) parser.fail("Parser nesting exceeds 256 levels.");
    ++parser.depth;
}
[[noreturn]] void Parser::fail(const std::string& message) {
    const auto& t = cursor.peek();
    diagnostics.report({driver::SourceKind::BufferNoAST, module.get_path(), "", t.line, t.column},
        "Error: " + message, "", "", driver::DiagnosticsLevel::Error);
    if (++errors >= 32) throw LimitError{};
    throw ParseError{};
}
const lexer::token_t& Parser::expect(TokenType t, const std::string& message) {
    if (!cursor.at(t)) fail(message);
    return cursor.advance();
}
ast::SourceSpan Parser::span(const lexer::token_t& start) const {
    return {id, start.offset, std::max(start.end_offset, cursor.previous().end_offset), start.line, start.column};
}
void Parser::newlines() { while (cursor.accept(Newline)) {} }
void Parser::separators() { while (cursor.accept(Newline) || cursor.accept(Semicolon)) {} }
void Parser::terminate() {
    if (cursor.accept(Semicolon)) return;
    if (cursor.at(Newline) || cursor.at(Dedent) || cursor.at(RightBrace) || cursor.at(EndOfFile)) return;
    fail("Expected a newline or ';' after the statement.");
}
void Parser::recover() {
    while (!cursor.at(EndOfFile) && !cursor.at(RightBrace) && !cursor.at(Dedent)) {
        if (cursor.accept(Newline) || cursor.accept(Semicolon)) return;
        cursor.advance();
    }
}
std::unique_ptr<ast::ModuleAST> Parser::parse() {
    const auto& tokens = module.get_tokens();
    if (tokens.empty() || tokens.back().type != EndOfFile ||
        std::count_if(tokens.begin(), tokens.end(), [](const auto& t) { return t.type == EndOfFile; }) != 1) {
        diagnostics.report({driver::SourceKind::BufferNoAST, module.get_path(), "", 1, 1},
            "Error: Invalid parser token stream; expected exactly one final EOF.", "", "", driver::DiagnosticsLevel::Error);
        return nullptr;
    }
    auto result = std::make_unique<ast::ModuleAST>();
    result->module = id;
    try {
        separators();
        while (!cursor.at(EndOfFile)) {
            const auto before = cursor.position();
            try { result->statements.push_back(statement(true)); }
            catch (const ParseError&) { recover(); }
            if (cursor.position() == before) cursor.advance();
            separators();
        }
    } catch (const LimitError&) { return nullptr; }
    return errors == 0 ? std::move(result) : nullptr;
}
ast::TypeRef Parser::type() {
    const auto start = cursor.peek();
    if (!basic_type(start.type) && start.type != Identifier) fail("Expected a type name.");
    cursor.advance();
    return {span(start), start.value};
}
ast::StmtPtr Parser::function() {
    const auto start = cursor.advance();
    const auto name = expect(Identifier, "Expected a function name.").value;
    auto result = std::make_unique<ast::FunctionDecl>(span(start), name);
    expect(LeftParen, "Expected '(' after the function name.");
    if (!cursor.at(RightParen)) {
        do {
            const auto first = cursor.peek();
            if (cursor.at(Identifier) && cursor.peek(1).type == Colon) {
                cursor.advance(); cursor.advance();
                auto parameter_type = type();
                result->parameters.push_back({span(first), first.value, std::move(parameter_type)});
            } else {
                auto parameter_type = type();
                const auto parameter_name = expect(Identifier, "Expected a parameter name.").value;
                result->parameters.push_back({span(first), parameter_name, std::move(parameter_type)});
            }
        } while (cursor.accept(Comma));
    }
    expect(RightParen, "Expected ')' after parameters.");
    if (cursor.accept(Arrow)) result->return_type = type();
    result->body = block();
    result->span = span(start);
    return result;
}
ast::StmtPtr Parser::variable() {
    const auto start = cursor.peek();
    auto declared_type = type();
    const auto name = expect(Identifier, "Expected a variable name.").value;
    ast::ExprPtr value;
    if (cursor.accept(Equal)) { newlines(); value = expression(); }
    const auto location = span(start);
    terminate();
    return std::make_unique<ast::VariableDecl>(location, std::move(declared_type), name, std::move(value));
}
std::unique_ptr<ast::Block> Parser::block() {
    Depth guard(*this);
    const auto start = cursor.peek();
    TokenType close;
    if (cursor.accept(Colon)) {
        expect(Newline, "Expected a newline after ':'."); newlines();
        expect(Indent, "Expected an indented block after ':'.");
        close = Dedent;
    } else {
        newlines();
        expect(LeftBrace, "Expected ':' or '{' to begin a block.");
        close = RightBrace;
    }
    auto result = std::make_unique<ast::Block>(span(start));
    separators();
    while (!cursor.at(close) && !cursor.at(EndOfFile)) {
        const auto before = cursor.position();
        try { result->statements.push_back(statement()); }
        catch (const ParseError&) { recover(); }
        if (cursor.position() == before) cursor.advance();
        separators();
    }
    if (close == Dedent && result->statements.empty()) fail("An indented block must contain a statement.");
    expect(close, close == RightBrace ? "Expected '}' to close the block." : "Expected the end of the indented block.");
    result->span = span(start);
    return result;
}
ast::StmtPtr Parser::conditional() {
    Depth guard(*this);
    const auto start = cursor.advance(); // if or elif
    auto result = std::make_unique<ast::IfStmt>(span(start));
    result->condition = expression();
    result->then_branch = block();
    // Only newlines, never a semicolon, may separate a block and its else/elif.
    newlines();
    if (cursor.at(Elif)) result->else_branch = conditional();
    else if (cursor.accept(Else)) result->else_branch = block();
    result->span = span(start);
    return result;
}
ast::StmtPtr Parser::statement(bool top_level) {
    Depth guard(*this);
    const auto start = cursor.peek();
    if (cursor.at(Function)) {
        if (!top_level) fail("Nested function declarations are not supported yet.");
        return function();
    }
    if (basic_type(start.type) || (start.type == Identifier && cursor.peek(1).type == Identifier)) return variable();
    if (cursor.at(LeftBrace)) return block();
    if (cursor.at(If)) return conditional();
    if (cursor.accept(WhileLoop)) {
        auto result = std::make_unique<ast::WhileStmt>(span(start));
        result->condition = expression(); result->body = block(); result->span = span(start);
        return result;
    }
    if (cursor.accept(Return)) {
        ast::ExprPtr value;
        if (!cursor.at(Newline) && !cursor.at(Semicolon) && !cursor.at(Dedent) &&
            !cursor.at(RightBrace) && !cursor.at(EndOfFile)) value = expression();
        const auto location = span(start); terminate();
        return std::make_unique<ast::ReturnStmt>(location, std::move(value));
    }
    if (cursor.accept(Break)) { terminate(); return std::make_unique<ast::BreakStmt>(span(start)); }
    if (cursor.accept(Continue)) { terminate(); return std::make_unique<ast::ContinueStmt>(span(start)); }
    switch (start.type) {
        case ForLoop: case Switch: case Try: case Catch: case Throw: case Enum:
        case Struct: case Class: case Lambda: case Asm: case New: case Delete:
        case Const: case Pointer: case Array: case Tuple: case Dict: case FString:
            fail(std::string("Syntax not supported yet: ") + lexer::to_string_token(start.type) + ".");
        default: break;
    }
    auto value = expression();
    const auto location = span(start); terminate();
    return std::make_unique<ast::ExprStmt>(location, std::move(value));
}
ast::ExprPtr Parser::expression(int minimum) {
    Depth guard(*this);
    auto left = unary();
    unsigned operators = 0;
    while (precedence(cursor.peek().type) >= minimum) {
        if (++operators > 256) fail("Expression exceeds 256 operators.");
        const auto op = cursor.advance();
        const auto p = precedence(op.type);
        newlines(); // A trailing operator explicitly continues the expression.
        auto right = expression(op.type == Equal ? p : p + 1);
        auto location = left->span; location.end = right->span.end;
        const auto height = 1 + std::max(left->height, right->height);
        if (op.type == Equal) {
            if (!dynamic_cast<ast::NameExpr*>(left.get()) && !dynamic_cast<ast::MemberExpr*>(left.get()))
                fail("Invalid assignment target; expected a name or member access.");
            left = bounded(std::make_unique<ast::AssignExpr>(location, std::move(left), std::move(right)), height);
        } else left = bounded(std::make_unique<ast::BinaryExpr>(location, op.type, std::move(left), std::move(right)), height);
    }
    return left;
}
ast::ExprPtr Parser::unary() {
    Depth guard(*this);
    const auto start = cursor.peek();
    if (cursor.accept(Plus) || cursor.accept(Minus) || cursor.accept(Not) || cursor.accept(Not_C)) {
        newlines();
        auto operand = unary();
        const auto height = 1 + operand->height;
        return bounded(std::make_unique<ast::UnaryExpr>(span(start), start.type, std::move(operand)), height);
    }
    auto result = primary();
    unsigned suffixes = 0;
    // A call/member suffix must begin on the same logical line as its callee.
    while (cursor.at(LeftParen) || cursor.at(Point)) {
        if (++suffixes > 256) fail("Expression exceeds 256 postfix operations.");
        if (cursor.accept(Point)) {
            const auto member = expect(Identifier, "Expected a member name after '.'.").value;
            const auto height = 1 + result->height;
            result = bounded(std::make_unique<ast::MemberExpr>(span(start), std::move(result), member), height);
        } else {
            cursor.advance();
            auto call = std::make_unique<ast::CallExpr>(span(start), std::move(result));
            if (!cursor.at(RightParen)) {
                do { call->arguments.push_back(expression()); } while (cursor.accept(Comma));
            }
            expect(RightParen, "Expected ')' after call arguments.");
            auto height = call->callee->height + 1;
            for (const auto& argument : call->arguments) height = std::max(height, argument->height + 1);
            call->span = span(start); result = bounded(std::move(call), height);
        }
    }
    return result;
}
ast::ExprPtr Parser::bounded(ast::ExprPtr value, unsigned height) {
    if (height > 256) fail("Expression tree exceeds 256 levels.");
    value->height = height;
    return value;
}
ast::ExprPtr Parser::primary() {
    const auto start = cursor.peek();
    if (cursor.accept(Identifier)) return std::make_unique<ast::NameExpr>(span(start), start.value);
    if (cursor.accept(LeftParen)) {
        auto value = expression();
        expect(RightParen, "Expected ')' after the expression.");
        value->span = span(start); return value;
    }
    if (cursor.accept(RawString)) {
        const auto value = expect(StringLiteral, "Expected a string after the raw-string prefix.").value;
        return std::make_unique<ast::LiteralExpr>(span(start), StringLiteral, value);
    }
    switch (start.type) {
        case IntegerLiteral: case FloatLiteral: case StringLiteral: case CharLiteral:
        case LiteralTrue: case LiteralFalse: case Null:
            cursor.advance(); return std::make_unique<ast::LiteralExpr>(span(start), start.type, start.value);
        case FString: case Lambda: case New: case Delete:
            fail(std::string("Expression not supported yet: ") + lexer::to_string_token(start.type) + ".");
        default: fail(std::string("Expected an expression; found ") + lexer::to_string_token(start.type) + ".");
    }
}
} // namespace optic::compiler::parser
