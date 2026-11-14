#include "compiler/parser/parser.hpp"
namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;
ast::StmtPtr Parser::function(bool constructor) {
    const auto start = cursor.peek();
    if (!constructor) cursor.advance();
    const auto name = expect(Identifier, "Expected a function name.").value;
    auto result = std::make_unique<ast::FunctionDecl>(span(start), name);
    result->constructor = constructor;
    expect(LeftParen, "Expected '(' after the function name.");
    {
        SoftLines lines(*this); newlines();
        if (!cursor.at(RightParen)) {
            do {
                newlines();
                const auto first = cursor.peek();
                const auto parameter_name = expect(Identifier, "Expected a parameter name; use name: Type for annotations.").value;
                std::optional<ast::TypeRef> annotation;
                if (cursor.accept(Colon)) { newlines(); annotation = type(); }
                result->parameters.push_back({span(first), parameter_name, std::move(annotation)});
                newlines();
                if (!cursor.accept(Comma)) break;
                newlines();
                if (cursor.at(RightParen)) break;
            } while (true);
        }
        expect(RightParen, "Expected ')' after parameters.");
    }
    if (cursor.accept(Arrow)) { newlines(); result->return_type = type(); }
    result->body = block(); result->span = span(start);
    return result;
}
ast::StmtPtr Parser::variable(bool end) {
    const auto start = cursor.peek();
    auto declared_type = type(false);
    auto group = std::make_unique<ast::DeclGroup>(span(start));
    do {
        const auto declaration_start = cursor.peek();
        const auto name = expect(Identifier, "Expected a variable name after its type.").value;
        ast::ExprPtr value;
        if (cursor.accept(Equal)) { newlines(); value = expression(); }
        group->declarations.push_back(std::make_unique<ast::VariableDecl>(span(declaration_start), declared_type, name, std::move(value)));
    } while (cursor.accept(Comma));
    group->span = span(start);
    if (end) terminate();
    if (group->declarations.size() == 1) {
        auto result = std::move(group->declarations.front()); result->span = group->span; return result;
    }
    return group;
}
ast::StmtPtr Parser::for_statement() {
    const auto start = cursor.advance();
    auto result = std::make_unique<ast::ForStmt>(span(start));
    expect(LeftParen, "Expected '(' after 'for'.");
    {
        SoftLines lines(*this); newlines();
        if (!cursor.at(Semicolon)) {
            if (declaration_ahead()) result->initialization = variable(false);
            else {
                const auto first = cursor.peek();
                auto value = expression();
                result->initialization = std::make_unique<ast::ExprStmt>(span(first), std::move(value));
            }
        }
        newlines(); expect(Semicolon, "Expected ';' after the for initialization."); newlines();
        if (!cursor.at(Semicolon)) result->condition = expression();
        newlines(); expect(Semicolon, "Expected ';' after the for condition."); newlines();
        if (!cursor.at(RightParen)) {
            do { result->updates.push_back(expression()); newlines(); } while (cursor.accept(Comma));
        }
        expect(RightParen, "Expected ')' after the for update.");
    }
    result->body = block(); result->span = span(start);
    return result;
}
ast::StmtPtr Parser::record() {
    Depth guard(*this);
    const auto start = cursor.advance();
    const auto name = expect(Identifier, "Expected a class or struct name.").value;
    auto result = std::make_unique<ast::RecordDecl>(span(start), name, start.type == Class);
    newlines(); expect(LeftBrace, "Expected '{' to begin the record."); separators();
    while (!cursor.at(RightBrace) && !cursor.at(EndOfFile)) {
        const auto before = cursor.position();
        try {
            std::string visibility;
            if (cursor.at(Public) || cursor.at(Private) || cursor.at(Protected)) visibility = cursor.advance().value;
            ast::StmtPtr declaration;
            if (cursor.at(Function)) declaration = function();
            else if (cursor.at(Identifier) && cursor.peek().value == name && cursor.peek(1).type == LeftParen) declaration = function(true);
            else if (cursor.at(Alias)) declaration = alias();
            else declaration = variable();
            result->members.push_back({visibility, std::move(declaration)});
        } catch (const ParseError&) { recover(); }
        if (cursor.position() == before) cursor.advance();
        separators();
    }
    expect(RightBrace, "Expected '}' to close the record."); result->span = span(start);
    return result;
}
ast::StmtPtr Parser::try_statement() {
    const auto start = cursor.advance();
    auto result = std::make_unique<ast::TryStmt>(span(start));
    result->body = block(); newlines();
    if (!cursor.at(Catch)) fail("Expected at least one 'catch' after 'try'.");
    while (cursor.at(Catch)) {
        const auto handler_start = cursor.advance();
        expect(LeftParen, "Expected '(' after 'catch'.");
        std::optional<ast::TypeRef> exception_type;
        std::string name;
        {
            SoftLines lines(*this); newlines(); exception_type = type();
            name = expect(Identifier, "Expected an exception binding name.").value;
            newlines(); expect(RightParen, "Expected ')' after the exception binding.");
        }
        auto body = block();
        result->handlers.push_back({span(handler_start), std::move(*exception_type), name, std::move(body)});
        newlines();
    }
    result->span = span(start); return result;
}
ast::StmtPtr Parser::alias() {
    const auto start = cursor.advance();
    const bool grouped = cursor.accept(LeftBracket);
    std::optional<ast::TypeRef> target;
    if (grouped) {
        SoftLines lines(*this); newlines(); target = type(); newlines();
        expect(RightBracket, "Expected ']' around the alias type expression.");
    } else {
        if (!basic_type(cursor.peek().type) && !cursor.at(Identifier)) fail("Expected a type name or a bracketed type expression after 'alias'.");
        target = type(false);
        if (!target->arguments.empty() || target->pointers || target->reference || target->constant)
            fail("Enclose a compound alias target in '[' and ']'.");
    }
    expect(As, "Expected 'as' between the original type and its new name.");
    const auto name = expect(Identifier, "Expected a new alias name.").value;
    auto result = std::make_unique<ast::AliasDecl>(span(start), name);
    result->type_target = std::move(target);
    terminate(); return result;
}
}
