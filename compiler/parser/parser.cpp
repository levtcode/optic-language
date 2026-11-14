#include "compiler/parser/parser.hpp"
#include "compiler/lexer/lexer.hpp"
#include <algorithm>

namespace optic::compiler::parser {
using lexer::TokenType;
using enum TokenType;

Parser::Depth::Depth(Parser& parser)
    : parser(parser) {
    if (parser.depth >= max_nesting_depth) {
        parser.fail("Parser nesting exceeds 128 levels.");
    }
    ++parser.depth;
}

[[noreturn]] void Parser::fail(const std::string& message) {
    if (probing) {
        throw ParseError{};
    }
    const auto& t = cursor.peek();
    diagnostics.report({driver::SourceKind::BufferNoAST, module.get_path(), "", t.line, t.column},
                       "Error: " + message,
                       "",
                       "",
                       driver::DiagnosticsLevel::Error);
    if (++errors >= max_syntax_errors) {
        throw LimitError{};
    }
    throw ParseError{};
}

const lexer::token_t& Parser::expect(TokenType t, const std::string& message) {
    if (!cursor.at(t)) {
        fail(message);
    }
    return cursor.advance();
}

ast::SourceSpan Parser::span(const lexer::token_t& start) const {
    return {id,
            start.offset,
            std::max(start.end_offset, cursor.previous().end_offset),
            start.line,
            start.column};
}

void Parser::skip_newlines() {
    while (cursor.accept(Newline)) {
    }
}

void Parser::skip_separators() {
    while (cursor.accept(Newline) || cursor.accept(Semicolon)) {
    }
}

void Parser::expect_statement_end() {
    if (cursor.accept(Semicolon)) {
        return;
    }
    if (cursor.at(Newline) || cursor.at(RightBrace) || cursor.at(EndOfFile)) {
        return;
    }
    fail("Expected a newline or ';' after the statement.");
}

void Parser::recover_statement() {
    while (!cursor.at(EndOfFile) && !cursor.at(RightBrace)) {
        if (cursor.accept(Newline) || cursor.accept(Semicolon)) {
            return;
        }
        cursor.advance();
    }
}

std::unique_ptr<ast::ModuleAST> Parser::parse() {
    const auto& tokens = module.get_tokens();
    if (tokens.empty() || tokens.back().type != EndOfFile ||
        std::count_if(
            tokens.begin(), tokens.end(), [](const auto& t) { return t.type == EndOfFile; }) != 1) {
        diagnostics.report({driver::SourceKind::BufferNoAST, module.get_path(), "", 1, 1},
                           "Error: Invalid parser token stream; expected exactly one final EOF.",
                           "",
                           "",
                           driver::DiagnosticsLevel::Error);
        return nullptr;
    }
    // Forward references affect only syntactic disambiguation, never type resolution.
    for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
        if ((tokens[i].type == Class || tokens[i].type == Struct || tokens[i].type == As) &&
            tokens[i + 1].type == Identifier) {
            type_names.insert(tokens[i + 1].value);
        }
    }
    auto result = std::make_unique<ast::ModuleAST>();
    result->module = id;
    try {
        skip_separators();
        while (!cursor.at(EndOfFile)) {
            const auto before = cursor.position();
            try {
                result->statements.push_back(parse_statement(true));
            } catch (const ParseError&) {
                recover_statement();
            }
            if (cursor.position() == before) {
                cursor.advance();
            }
            skip_separators();
        }
    } catch (const LimitError&) {
        return nullptr;
    }
    return errors == 0 ? std::move(result) : nullptr;
}

std::unique_ptr<ast::Block> Parser::parse_block() {
    Depth guard(*this);
    // Newlines inside a block terminate its statements even if an enclosing expression
    // has soft skip_newlines (e.g. collection literal containing a call).
    skip_newlines();
    const auto start =
        expect(LeftBrace, "Expected '{' to begin a block; ':' blocks are not supported.");
    auto result = std::make_unique<ast::Block>(span(start));
    skip_separators();
    while (!cursor.at(RightBrace) && !cursor.at(EndOfFile)) {
        const auto before = cursor.position();
        try {
            result->statements.push_back(parse_statement());
        } catch (const ParseError&) {
            recover_statement();
        }
        if (cursor.position() == before) {
            cursor.advance();
        }
        skip_separators();
    }
    expect(RightBrace, "Expected '}' to close the block.");
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_if_statement() {
    Depth guard(*this);
    const auto start = expect(If, "Expected 'if'.");
    auto result = std::make_unique<ast::IfStmt>(span(start));
    result->condition = parse_expression();
    result->then_branch = parse_block();
    skip_newlines();
    if (cursor.accept(Else)) {
        skip_newlines();
        if (cursor.at(If)) {
            result->else_branch = parse_if_statement();
        } else {
            result->else_branch = parse_block();
        }
    }
    result->span = span(start);
    return result;
}

ast::StmtPtr Parser::parse_statement(bool declarations) {
    Depth guard(*this);
    const auto start = cursor.peek();
    if (cursor.at(Function)) {
        if (!declarations) {
            fail("Nested function declarations are not supported yet.");
        }
        return parse_function();
    }
    if (cursor.at(Class) || cursor.at(Struct)) {
        if (!declarations) {
            fail("Record declarations must be at module scope.");
        }
        return parse_record();
    }
    if (cursor.at(Alias)) {
        return parse_type_alias();
    }
    if (cursor.at(LeftBrace)) {
        return parse_block();
    }
    if (cursor.at(If)) {
        return parse_if_statement();
    }
    if (cursor.at(ForLoop)) {
        return parse_for_statement();
    }
    if (cursor.at(Try)) {
        return parse_try_statement();
    }
    if (cursor.accept(WhileLoop)) {
        auto result = std::make_unique<ast::WhileStmt>(span(start));
        result->condition = parse_expression();
        result->body = parse_block();
        result->span = span(start);
        return result;
    }
    if (cursor.accept(Return) || cursor.accept(Throw)) {
        ast::ExprPtr value;
        if (!cursor.at(Newline) && !cursor.at(Semicolon) && !cursor.at(RightBrace) &&
            !cursor.at(EndOfFile)) {
            value = parse_expression();
        }
        if (start.type == Throw && !value) {
            fail("Expected an exception expression after 'throw'.");
        }
        const auto location = span(start);
        expect_statement_end();
        if (start.type == Throw) {
            return std::make_unique<ast::ThrowStmt>(location, std::move(value));
        }
        return std::make_unique<ast::ReturnStmt>(location, std::move(value));
    }
    if (cursor.accept(Break)) {
        const auto location = span(start);
        expect_statement_end();
        return std::make_unique<ast::BreakStmt>(location);
    }
    if (cursor.accept(Continue)) {
        const auto location = span(start);
        expect_statement_end();
        return std::make_unique<ast::ContinueStmt>(location);
    }
    if (is_declaration_ahead()) {
        const bool unresolved_pointer = cursor.at(Identifier) && !type_names.contains(start.value);
        if (!unresolved_pointer) {
            return parse_variable_declaration();
        }
        const auto begin = cursor.position();
        auto declaration = parse_variable_declaration();
        const auto end = cursor.position();
        cursor.restore(begin);
        const auto saved_probe = probing;
        probing = true;
        ast::ExprPtr alternative;
        try {
            alternative = parse_expression();
            expect_statement_end();
        } catch (const ParseError&) {
            // A failed lookahead is expected; normal parsing reports real errors.
        }
        probing = saved_probe;
        const bool same_end = cursor.position() == end;
        cursor.restore(end);
        if (alternative && same_end) {
            return std::make_unique<ast::AmbiguousStmt>(
                span(start), std::move(declaration), std::move(alternative));
        }
        return declaration;
    }
    auto value = parse_expression();
    const auto location = span(start);
    expect_statement_end();
    return std::make_unique<ast::ExprStmt>(location, std::move(value));
}

ast::ExprPtr Parser::check_expression_height(ast::ExprPtr value, unsigned height) {
    if (height > max_expression_height) {
        fail("Expression tree exceeds 256 levels.");
    }
    value->height = height;
    return value;
}
} // namespace optic::compiler::parser
