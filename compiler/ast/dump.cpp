#include "compiler/ast/dump.hpp"
#include "compiler/lexer/lexer.hpp"
#include <iomanip>
#include <ostream>
namespace optic::compiler::ast {
namespace {
void node(const Node& n, std::ostream& out, unsigned depth) {
    out << std::string(depth * 2, ' ');
    auto child = [&](const auto& p) { if (p) node(*p, out, depth + 1); };
    auto label = [&](const std::string& text) {
        out << text << " @" << n.span.line << ':' << n.span.column << '\n';
    };
    if (const auto* v = dynamic_cast<const NameExpr*>(&n)) label("Name " + v->name);
    else if (const auto* v = dynamic_cast<const LiteralExpr*>(&n)) {
        out << "Literal " << lexer::to_string_token(v->kind) << ' ' << std::quoted(v->value)
            << " @" << n.span.line << ':' << n.span.column << '\n';
    } else if (const auto* v = dynamic_cast<const UnaryExpr*>(&n)) {
        label(std::string("Unary ") + lexer::to_string_token(v->op)); child(v->operand);
    } else if (const auto* v = dynamic_cast<const BinaryExpr*>(&n)) {
        label(std::string("Binary ") + lexer::to_string_token(v->op)); child(v->left); child(v->right);
    } else if (const auto* v = dynamic_cast<const AssignExpr*>(&n)) {
        label("Assign"); child(v->target); child(v->value);
    } else if (const auto* v = dynamic_cast<const MemberExpr*>(&n)) {
        label("Member " + v->member); child(v->object);
    } else if (const auto* v = dynamic_cast<const CallExpr*>(&n)) {
        label("Call"); child(v->callee); for (const auto& a : v->arguments) child(a);
    } else if (const auto* v = dynamic_cast<const Block*>(&n)) {
        label("Block"); for (const auto& s : v->statements) child(s);
    } else if (const auto* v = dynamic_cast<const VariableDecl*>(&n)) {
        label("Variable " + v->name + ": " + v->type.name); child(v->initializer);
    } else if (const auto* v = dynamic_cast<const FunctionDecl*>(&n)) {
        label("Function " + v->name + (v->return_type ? " -> " + v->return_type->name : ""));
        for (const auto& p : v->parameters)
            out << std::string((depth + 1) * 2, ' ') << "Parameter " << p.name << ": " << p.type.name << '\n';
        child(v->body);
    } else if (const auto* v = dynamic_cast<const IfStmt*>(&n)) {
        label("If"); child(v->condition); child(v->then_branch); child(v->else_branch);
    } else if (const auto* v = dynamic_cast<const WhileStmt*>(&n)) {
        label("While"); child(v->condition); child(v->body);
    } else if (const auto* v = dynamic_cast<const ReturnStmt*>(&n)) {
        label("Return"); child(v->value);
    } else if (const auto* v = dynamic_cast<const ExprStmt*>(&n)) {
        label("Expression"); child(v->expression);
    } else if (dynamic_cast<const BreakStmt*>(&n)) label("Break");
    else if (dynamic_cast<const ContinueStmt*>(&n)) label("Continue");
}
}
void dump(const ModuleAST& module, std::ostream& out) {
    out << "ModuleAST [" << module.module << "]\n";
    for (const auto& statement : module.statements) node(*statement, out, 1);
}
}
