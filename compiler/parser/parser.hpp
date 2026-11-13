/** @file parser.hpp
 * @brief One parser per module; results outlive the parser and own their nodes.
 */
#pragma once
#include "compiler/ast/ast.hpp"
#include "compiler/parser/token_cursor.hpp"

namespace optic::compiler::parser {
class Parser {
public:
    Parser(driver::ModuleID id, const driver::Module& module, driver::DiagnosticsEngine& diagnostics)
        : id(id), module(module), diagnostics(diagnostics), cursor(module.get_tokens()) {}
    /// Null on errors. Never publishes a partial AST as valid input to semantic analysis.
    std::unique_ptr<ast::ModuleAST> parse();
private:
    struct ParseError {};
    struct LimitError {};
    struct Depth {
        Parser& parser;
        explicit Depth(Parser& parser);
        ~Depth() { --parser.depth; }
    };
    driver::ModuleID id;
    const driver::Module& module;
    driver::DiagnosticsEngine& diagnostics;
    TokenCursor cursor;
    unsigned depth = 0, errors = 0;

    [[noreturn]] void fail(const std::string& message);
    const lexer::token_t& expect(lexer::TokenType type, const std::string& message);
    ast::SourceSpan span(const lexer::token_t& start) const;
    void newlines();
    void separators();
    void terminate();
    void recover();
    ast::StmtPtr statement(bool top_level = false);
    std::unique_ptr<ast::Block> block();
    ast::StmtPtr function();
    ast::StmtPtr variable();
    ast::StmtPtr conditional();
    ast::TypeRef type();
    ast::ExprPtr expression(int minimum = 1);
    ast::ExprPtr unary();
    ast::ExprPtr primary();
    ast::ExprPtr bounded(ast::ExprPtr value, unsigned height);
};
}
