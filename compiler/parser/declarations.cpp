#include "compiler/parser/parser.hpp"

namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;

ast::StmtPtr Parser::parse_function(bool constructor) {
    const auto start = cursor.peek();
    if (!constructor) {
        cursor.advance();
    }
    const auto name = expect(Identifier, "Expected a function name.").value;
    auto result = std::make_unique<ast::FunctionDecl>(span(start), name);
    result->constructor = constructor;
    expect(LeftParen, "Expected '(' after the function name.");
    {
        SoftLines lines(*this);
        skip_newlines();
        if (!cursor.at(RightParen)) {
            do {
                skip_newlines();
                const auto first = cursor.peek();
                const auto parameter_name =
                    expect(Identifier, "Expected a parameter name; use name: Type for annotations.")
                        .value;
                std::optional<ast::TypeRef> annotation;
                if (cursor.accept(Colon)) {
                    skip_newlines();
                    annotation = parse_type();
                }
                result->parameters.push_back({span(first), parameter_name, std::move(annotation)});
                skip_newlines();
                if (!cursor.accept(Comma)) {
                    break;
                }
                skip_newlines();
                if (cursor.at(RightParen)) {
                    break;
                }
            } while (true);
        }
        expect(RightParen, "Expected ')' after parameters.");
    }
    if (cursor.accept(Arrow)) {
        skip_newlines();
        result->return_type = parse_type();
    }
    result->body = parse_block();
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_variable_declaration(bool end) {
    const auto start = cursor.peek();
    auto declared_type = parse_type(false);
    auto group = std::make_unique<ast::DeclGroup>(span(start));
    do {
        const auto declaration_start = cursor.peek();
        const auto name = expect(Identifier, "Expected a variable name after its type.").value;
        ast::ExprPtr value;
        if (cursor.accept(Equal)) {
            skip_newlines();
            value = parse_expression();
        }
        group->declarations.push_back(std::make_unique<ast::VariableDecl>(
            span(declaration_start), declared_type, name, std::move(value)));
    } while (cursor.accept(Comma));
    group->span = span(start);
    if (end) {
        expect_statement_end();
    }
    if (group->declarations.size() == 1) {
        auto result = std::move(group->declarations.front());
        result->span = group->span;
        return result;
    }
    return group;
}

ast::StmtPtr Parser::parse_for_statement() {
    const auto start = cursor.advance();
    auto result = std::make_unique<ast::ForStmt>(span(start));
    expect(LeftParen, "Expected '(' after 'for'.");
    {
        SoftLines lines(*this);
        skip_newlines();
        if (!cursor.at(Semicolon)) {
            if (is_declaration_ahead()) {
                result->initialization = parse_variable_declaration(false);
            } else {
                const auto first = cursor.peek();
                auto value = parse_expression();
                result->initialization =
                    std::make_unique<ast::ExprStmt>(span(first), std::move(value));
            }
        }
        skip_newlines();
        expect(Semicolon, "Expected ';' after the for initialization.");
        skip_newlines();
        if (!cursor.at(Semicolon)) {
            result->condition = parse_expression();
        }
        skip_newlines();
        expect(Semicolon, "Expected ';' after the for condition.");
        skip_newlines();
        if (!cursor.at(RightParen)) {
            do {
                result->updates.push_back(parse_expression());
                skip_newlines();
            } while (cursor.accept(Comma));
        }
        expect(RightParen, "Expected ')' after the for update.");
    }
    result->body = parse_block();
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_record() {
    Depth guard(*this);
    const auto start = cursor.advance();
    const auto name = expect(Identifier, "Expected a class or struct name.").value;
    auto result = std::make_unique<ast::RecordDecl>(span(start), name, start.type == Class);
    skip_newlines();
    expect(LeftBrace, "Expected '{' to begin the record.");
    skip_separators();
    while (!cursor.at(RightBrace) && !cursor.at(EndOfFile)) {
        const auto before = cursor.position();
        try {
            std::string visibility;
            if (cursor.at(Public) || cursor.at(Private) || cursor.at(Protected)) {
                visibility = cursor.advance().value;
            }
            ast::StmtPtr declaration;
            if (cursor.at(Function)) {
                declaration = parse_function();
            } else if (cursor.at(Identifier) && cursor.peek().value == name &&
                       cursor.peek(1).type == LeftParen) {
                declaration = parse_function(true);
            } else if (cursor.at(Alias)) {
                declaration = parse_type_alias();
            } else {
                declaration = parse_variable_declaration();
            }
            result->members.push_back({visibility, std::move(declaration)});
        } catch (const ParseError&) {
            recover_statement();
        }
        if (cursor.position() == before) {
            cursor.advance();
        }
        skip_separators();
    }
    expect(RightBrace, "Expected '}' to close the record.");
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_try_statement() {
    const auto start = cursor.advance();
    auto result = std::make_unique<ast::TryStmt>(span(start));
    result->body = parse_block();
    skip_newlines();
    if (!cursor.at(Catch)) {
        fail("Expected at least one 'catch' after 'try'.");
    }
    while (cursor.at(Catch)) {
        const auto handler_start = cursor.advance();
        expect(LeftParen, "Expected '(' after 'catch'.");
        std::optional<ast::TypeRef> exception_type;
        std::string name;
        {
            SoftLines lines(*this);
            skip_newlines();
            exception_type = parse_type();
            name = expect(Identifier, "Expected an exception binding name.").value;
            skip_newlines();
            expect(RightParen, "Expected ')' after the exception binding.");
        }
        auto body = parse_block();
        result->handlers.push_back(
            {span(handler_start), std::move(*exception_type), name, std::move(body)});
        skip_newlines();
    }
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_type_alias() {
    const auto start = cursor.advance();
    const bool grouped = cursor.accept(LeftBracket);
    std::optional<ast::TypeRef> target;
    if (grouped) {
        SoftLines lines(*this);
        skip_newlines();
        target = parse_type();
        skip_newlines();
        expect(RightBracket, "Expected ']' around the alias type expression.");
    } else {
        if (!is_builtin_type(cursor.peek().type) && !cursor.at(Identifier)) {
            fail("Expected a type name or a bracketed type expression after 'alias'.");
        }
        target = parse_type(false);
        if (!target->arguments.empty() || target->pointers || target->reference ||
            target->constant) {
            fail("Enclose a compound alias target in '[' and ']'.");
        }
    }
    expect(As, "Expected 'as' between the original type and its new name.");
    const auto name = expect(Identifier, "Expected a new alias name.").value;
    auto result = std::make_unique<ast::AliasDecl>(span(start), name);
    result->type_target = std::move(target);
    expect_statement_end();
    return result;
}
} // namespace optic::compiler::parser
