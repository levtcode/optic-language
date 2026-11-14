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
struct Expr : Node {
    using Node::Node;
    unsigned height = 1;
};
using ExprPtr = std::unique_ptr<Expr>;
/// Syntax only: names, applications, unions and declarator qualifiers.
struct TypeRef : Node {
    TypeRef(SourceSpan span, std::string name) : Node(span), name(std::move(name)) {}
    std::string name;
    std::vector<TypeRef> arguments;
    std::vector<TypeRef> alternatives;
    std::shared_ptr<const Expr> value_argument; // Non-type argument, e.g. array extent. Names stay unresolved.
    unsigned pointers = 0;
    bool reference = false, constant = false;
};
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
    lexer::TokenType op = lexer::TokenType::Equal;
};
struct MemberExpr : Expr {
    MemberExpr(SourceSpan span, ExprPtr object, std::string member)
        : Expr(span), object(std::move(object)), member(std::move(member)) {}
    ExprPtr object;
    std::string member;
    bool qualified = false; // :: rather than .
};
struct CallExpr : Expr {
    CallExpr(SourceSpan span, ExprPtr callee) : Expr(span), callee(std::move(callee)) {}
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};
struct IndexExpr : Expr {
    IndexExpr(SourceSpan s, ExprPtr object, ExprPtr index)
        : Expr(s), object(std::move(object)), index(std::move(index)) {}
    ExprPtr object, index;
};
struct PostfixExpr : Expr {
    PostfixExpr(SourceSpan s, lexer::TokenType op, ExprPtr operand)
        : Expr(s), op(op), operand(std::move(operand)) {}
    lexer::TokenType op;
    ExprPtr operand;
};
struct CastExpr : Expr {
    CastExpr(SourceSpan s, TypeRef type, ExprPtr value)
        : Expr(s), type(std::move(type)), value(std::move(value)) {}
    TypeRef type;
    ExprPtr value;
    // (Name) +/-/*/& expr also has a grouping/binary interpretation until name resolution.
    bool ambiguous = false;
};
/// Explicitly unresolved until semantic name lookup can distinguish a C-style cast
/// from grouping. Retain the whole expression, since the choice changes precedence.
struct UnresolvedExpr : Expr {
    UnresolvedExpr(SourceSpan s, ExprPtr provisional, std::vector<lexer::token_t> tokens)
        : Expr(s), provisional(std::move(provisional)), tokens(std::move(tokens)) {}
    ExprPtr provisional;
    std::vector<lexer::token_t> tokens;
};
struct CollectionExpr : Expr {
    enum class Kind { Array, Vector, Tuple, Dictionary, EmptyBraces };
    CollectionExpr(SourceSpan s, Kind kind) : Expr(s), kind(kind) {}
    Kind kind;
    std::vector<ExprPtr> elements; // Dictionaries alternate key, value.
};
struct NewExpr : Expr {
    NewExpr(SourceSpan s, TypeRef type) : Expr(s), type(std::move(type)) {}
    TypeRef type;
    std::vector<ExprPtr> arguments;
};
struct FormattedExpr : Expr {
    using Expr::Expr;
    std::vector<ExprPtr> parts; // Literal segments and embedded expressions, in order.
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
    std::optional<TypeRef> type; // No annotation: implicit static generic parameter.
};
struct FunctionDecl : Stmt {
    FunctionDecl(SourceSpan span, std::string name) : Stmt(span), name(std::move(name)) {}
    std::string name;
    bool constructor = false;
    std::vector<Parameter> parameters;
    std::optional<TypeRef> return_type;
    std::unique_ptr<Block> body;
};
struct IfStmt : Stmt {
    using Stmt::Stmt;
    ExprPtr condition;
    std::unique_ptr<Block> then_branch;
    StmtPtr else_branch; // Another IfStmt for else if, or a Block.
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
struct ForStmt : Stmt {
    using Stmt::Stmt;
    StmtPtr initialization;
    ExprPtr condition;
    std::vector<ExprPtr> updates;
    std::unique_ptr<Block> body;
};
struct DeclGroup : Stmt {
    using Stmt::Stmt;
    std::vector<StmtPtr> declarations;
};
struct AmbiguousStmt : Stmt {
    AmbiguousStmt(SourceSpan s, StmtPtr declaration, ExprPtr expression)
        : Stmt(s), declaration(std::move(declaration)), expression(std::move(expression)) {}
    StmtPtr declaration;
    ExprPtr expression;
};
struct AliasDecl : Stmt {
    AliasDecl(SourceSpan s, std::string name) : Stmt(s), name(std::move(name)) {}
    std::string name;
    std::optional<TypeRef> type_target;
};
struct RecordDecl : Stmt {
    RecordDecl(SourceSpan s, std::string name, bool is_class)
        : Stmt(s), name(std::move(name)), is_class(is_class) {}
    struct Member { std::string visibility; StmtPtr declaration; };
    std::string name;
    bool is_class;
    std::vector<Member> members;
};
struct TryStmt : Stmt {
    using Stmt::Stmt;
    struct Handler { SourceSpan span; TypeRef type; std::string name; std::unique_ptr<Block> body; };
    std::unique_ptr<Block> body;
    std::vector<Handler> handlers;
};
struct ThrowStmt : Stmt {
    ThrowStmt(SourceSpan s, ExprPtr value) : Stmt(s), value(std::move(value)) {}
    ExprPtr value;
};
struct ModuleAST {
    driver::ModuleID module;
    std::vector<StmtPtr> statements;
};
} // namespace optic::compiler::ast
