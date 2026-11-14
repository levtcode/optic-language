/** One immutable module stream in; a compiler-owned syntax tree out. */
#pragma once
#include "compiler/ast/ast.hpp"
#include "compiler/parser/token_cursor.hpp"
#include <unordered_set>

namespace optic::compiler::parser {
class Parser {
public:
    Parser(driver::ModuleID id,
           const driver::Module& module,
           driver::DiagnosticsEngine& diagnostics)
        : id(id),
          module(module),
          diagnostics(diagnostics),
          cursor(module.get_tokens()) {}

    std::unique_ptr<ast::ModuleAST> parse();

private:
    Parser(driver::ModuleID id,
           const driver::Module& module,
           driver::DiagnosticsEngine& diagnostics,
           std::span<const lexer::token_t> tokens)
        : id(id),
          module(module),
          diagnostics(diagnostics),
          cursor(tokens) {}

    static constexpr unsigned max_nesting_depth = 128;
    static constexpr unsigned max_expression_height = 256;
    static constexpr unsigned max_syntax_errors = 32;

    // Guards restore parser state when an error unwinds the current production.
    struct ParseError {};

    struct LimitError {};

    struct Depth {
        Parser& parser;
        explicit Depth(Parser& parser);

        ~Depth() {
            --parser.depth;
        }
    };

    struct SoftLines {
        Parser& parser;

        explicit SoftLines(Parser& p)
            : parser(p) {
            ++p.soft_lines;
        }

        ~SoftLines() {
            --parser.soft_lines;
        }
    };

    // Borrowed input and per-module parsing state.
    driver::ModuleID id;
    const driver::Module& module;
    driver::DiagnosticsEngine& diagnostics;
    TokenCursor cursor;
    unsigned depth = 0;
    unsigned errors = 0;
    unsigned soft_lines = 0;
    bool probing = false;
    unsigned expression_nesting = 0;
    unsigned ambiguous_casts = 0;
    std::unordered_set<std::string> type_names;

    // Token boundaries and error handling.
    [[noreturn]] void fail(const std::string& message);
    const lexer::token_t& expect(lexer::TokenType type, const std::string& message);
    ast::SourceSpan span(const lexer::token_t& start) const;
    void skip_newlines();
    void skip_separators();
    void expect_statement_end();
    void recover_statement();

    // Statements and declarations.
    bool is_declaration_ahead();
    ast::StmtPtr parse_statement(bool declarations = false);
    std::unique_ptr<ast::Block> parse_block();
    ast::StmtPtr parse_function(bool constructor = false);
    ast::StmtPtr parse_variable_declaration(bool end = true);
    ast::StmtPtr parse_if_statement();
    ast::StmtPtr parse_for_statement();
    ast::StmtPtr parse_record();
    ast::StmtPtr parse_try_statement();
    ast::StmtPtr parse_type_alias();

    // Type syntax.
    ast::TypeRef parse_type(bool union_allowed = true);
    ast::TypeRef parse_type_atom();

    // Expressions and literal contents.
    ast::ExprPtr parse_expression(int minimum_precedence = 1);
    ast::ExprPtr parse_binary_expression(int minimum_precedence);
    ast::ExprPtr parse_unary_expression();
    ast::ExprPtr try_parse_cast(const lexer::token_t& start);
    ast::ExprPtr parse_allocation(const lexer::token_t& start);
    ast::ExprPtr parse_postfix_expression(const lexer::token_t& start);
    ast::ExprPtr parse_primary_expression();
    ast::ExprPtr parse_collection(lexer::TokenType close, ast::CollectionExpr::Kind kind);
    ast::ExprPtr parse_formatted_string();
    ast::ExprPtr parse_embedded_expression(const std::string& source, const lexer::token_t& origin);
    ast::ExprPtr check_expression_height(ast::ExprPtr value, unsigned height);

    // Grammar predicates.
    static bool is_builtin_type(lexer::TokenType type);
    static bool is_assignment_operator(lexer::TokenType type);
    static bool is_assignable(const ast::Expr& expression);
};
} // namespace optic::compiler::parser
