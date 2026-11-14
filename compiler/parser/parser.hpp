/** One immutable module stream in; a compiler-owned syntax tree out. */
#pragma once
#include "compiler/ast/ast.hpp"
#include "compiler/parser/token_cursor.hpp"
#include <unordered_set>

namespace optic::compiler::parser {
class Parser {
public:
    Parser(driver::ModuleID id, const driver::Module& module, driver::DiagnosticsEngine& diagnostics)
        : id(id), module(module), diagnostics(diagnostics), cursor(module.get_tokens()) {}
    std::unique_ptr<ast::ModuleAST> parse();
private:
    Parser(driver::ModuleID id, const driver::Module& module, driver::DiagnosticsEngine& diagnostics,
           std::span<const lexer::token_t> tokens)
        : id(id), module(module), diagnostics(diagnostics), cursor(tokens) {}
    struct ParseError {};
    struct LimitError {};
    struct Depth {
        Parser& parser;
        explicit Depth(Parser& parser);
        ~Depth() { --parser.depth; }
    };
    struct SoftLines {
        Parser& parser;
        explicit SoftLines(Parser& p) : parser(p) { ++p.soft_lines; }
        ~SoftLines() { --parser.soft_lines; }
    };
    driver::ModuleID id;
    const driver::Module& module;
    driver::DiagnosticsEngine& diagnostics;
    TokenCursor cursor;
    unsigned depth = 0, errors = 0, soft_lines = 0;
    bool probing = false;
    unsigned expression_nesting = 0, ambiguous_casts = 0;
    std::unordered_set<std::string> type_names;

    [[noreturn]] void fail(const std::string& message);
    const lexer::token_t& expect(lexer::TokenType type, const std::string& message);
    ast::SourceSpan span(const lexer::token_t& start) const;
    void newlines();
    void separators();
    void terminate();
    void recover();
    bool declaration_ahead();
    ast::StmtPtr statement(bool declarations = false);
    std::unique_ptr<ast::Block> block();
    ast::StmtPtr function(bool constructor = false);
    ast::StmtPtr variable(bool end = true);
    ast::StmtPtr conditional();
    ast::StmtPtr for_statement();
    ast::StmtPtr record();
    ast::StmtPtr try_statement();
    ast::StmtPtr alias();
    ast::TypeRef type(bool union_allowed = true);
    ast::TypeRef type_atom();
    ast::ExprPtr expression(int minimum = 1);
    ast::ExprPtr expression_impl(int minimum);
    ast::ExprPtr unary();
    ast::ExprPtr primary();
    ast::ExprPtr collection(lexer::TokenType close, ast::CollectionExpr::Kind kind);
    ast::ExprPtr formatted();
    ast::ExprPtr fragment_expression(const std::string& source, const lexer::token_t& origin);
    ast::ExprPtr bounded(ast::ExprPtr value, unsigned height);
    static bool basic_type(lexer::TokenType type);
    static bool assignment(lexer::TokenType type);
    static bool assignable(const ast::Expr& expression);
};
}
