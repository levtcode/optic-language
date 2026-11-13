/** @file ast.hpp
 * @brief Compiler-owned syntax trees. No dependency on parser implementation state.
 */
#pragma once
#include "compiler/driver/module.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace optic::compiler::ast {
/// Half-open byte range in the source identified by module; synthetic boundaries are zero-width.
struct SourceSpan {
    driver::ModuleID module;
    std::size_t begin, end;
    unsigned line, column;
};
struct Node {
    explicit Node(SourceSpan span) : span(span) {}
    virtual ~Node() = default;
    SourceSpan span;
};
struct TypeRef : Node {
    TypeRef(SourceSpan span, std::string name) : Node(span), name(std::move(name)) {}
    std::string name; // Syntactic spelling, not a resolved semantic type.
};
struct Expr : Node {
    using Node::Node;
    unsigned height = 1;
};
using ExprPtr = std::unique_ptr<Expr>;
struct LiteralExpr : Expr {
    LiteralExpr(SourceSpan span, lexer::TokenType kind, std::string value)
        : Expr(span), kind(kind), value(std::move(value)) {}
    lexer::TokenType kind;
    std::string value; // Decoded strings; numeric spellings are retained without conversion.
};
struct NameExpr : Expr {
    NameExpr(SourceSpan span, std::string name) : Expr(span), name(std::move(name)) {}
    std::string name;
};
struct UnaryExpr : Expr {
    UnaryExpr(SourceSpan span, lexer::TokenType op, ExprPtr operand)
        : Expr(span), op(op), operand(std::move(operand)) {}
    lexer::TokenType op;
    ExprPtr operand;
};
struct BinaryExpr : Expr {
    BinaryExpr(SourceSpan span, lexer::TokenType op, ExprPtr left, ExprPtr right)
        : Expr(span), op(op), left(std::move(left)), right(std::move(right)) {}
    lexer::TokenType op;
    ExprPtr left, right;
};
struct AssignExpr : Expr {
    AssignExpr(SourceSpan span, ExprPtr target, ExprPtr value)
        : Expr(span), target(std::move(target)), value(std::move(value)) {}
    ExprPtr target, value;
};
struct MemberExpr : Expr {
    MemberExpr(SourceSpan span, ExprPtr object, std::string member)
        : Expr(span), object(std::move(object)), member(std::move(member)) {}
    ExprPtr object;
    std::string member;
};
struct CallExpr : Expr {
    CallExpr(SourceSpan span, ExprPtr callee) : Expr(span), callee(std::move(callee)) {}
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};
struct Stmt : Node { using Node::Node; };
using StmtPtr = std::unique_ptr<Stmt>;
struct Block : Stmt {
    using Stmt::Stmt;
    std::vector<StmtPtr> statements;
};
struct ExprStmt : Stmt {
    ExprStmt(SourceSpan span, ExprPtr expression) : Stmt(span), expression(std::move(expression)) {}
    ExprPtr expression;
};
struct VariableDecl : Stmt {
    VariableDecl(SourceSpan span, TypeRef type, std::string name, ExprPtr initializer)
        : Stmt(span), type(std::move(type)), name(std::move(name)), initializer(std::move(initializer)) {}
    TypeRef type;
    std::string name;
    ExprPtr initializer;
};
struct Parameter {
    SourceSpan span;
    std::string name;
    TypeRef type;
};
struct FunctionDecl : Stmt {
    FunctionDecl(SourceSpan span, std::string name) : Stmt(span), name(std::move(name)) {}
    std::string name;
    std::vector<Parameter> parameters;
    std::optional<TypeRef> return_type;
    std::unique_ptr<Block> body;
};
struct IfStmt : Stmt {
    using Stmt::Stmt;
    ExprPtr condition;
    std::unique_ptr<Block> then_branch;
    StmtPtr else_branch; // Another IfStmt for elif, or a Block.
};
struct WhileStmt : Stmt {
    using Stmt::Stmt;
    ExprPtr condition;
    std::unique_ptr<Block> body;
};
struct ReturnStmt : Stmt {
    ReturnStmt(SourceSpan span, ExprPtr value) : Stmt(span), value(std::move(value)) {}
    ExprPtr value;
};
struct BreakStmt : Stmt { using Stmt::Stmt; };
struct ContinueStmt : Stmt { using Stmt::Stmt; };
struct ModuleAST {
    driver::ModuleID module;
    std::vector<StmtPtr> statements;
};
} // namespace optic::compiler::ast
