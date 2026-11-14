#include "compiler/ast/dump.hpp"
#include "compiler/lexer/lexer.hpp"
#include <iomanip>
#include <ostream>
namespace optic::compiler::ast {
namespace {
std::string type_name(const TypeRef& type) {
    std::string name = type.constant ? "const " : "";
    name += type.name;
    if (type.value_argument) {
        if (const auto* literal = dynamic_cast<const LiteralExpr*>(type.value_argument.get())) name += literal->value;
        else if (const auto* symbol = dynamic_cast<const NameExpr*>(type.value_argument.get())) name += symbol->name;
        else name += "<expression>";
    }
    if (!type.arguments.empty()) {
        name += "[";
        for (const auto& argument : type.arguments) {
            if (name.back() != '[') name += ", ";
            name += type_name(argument);
        }
        name += "]";
    }
    for (const auto& alternative : type.alternatives) {
        if (!name.empty()) name += " | ";
        name += type_name(alternative);
    }
    name += std::string(type.pointers, '*');
    if (type.reference) name += "&";
    return name;
}
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
        label(std::string("Assign ") + lexer::to_string_token(v->op)); child(v->target); child(v->value);
    } else if (const auto* v = dynamic_cast<const MemberExpr*>(&n)) {
        label(std::string(v->qualified ? "Qualified " : "Member ") + v->member); child(v->object);
    } else if (const auto* v = dynamic_cast<const CallExpr*>(&n)) {
        label("Call"); child(v->callee); for (const auto& a : v->arguments) child(a);
    } else if (const auto* v = dynamic_cast<const AmbiguousStmt*>(&n)) {
        label("DeclarationOrExpression"); child(v->declaration); child(v->expression);
    } else if (const auto* v = dynamic_cast<const UnresolvedExpr*>(&n)) {
        label("UnresolvedExpression"); child(v->provisional);
    } else if (const auto* v = dynamic_cast<const IndexExpr*>(&n)) {
        label("Index"); child(v->object); child(v->index);
    } else if (const auto* v = dynamic_cast<const PostfixExpr*>(&n)) {
        label(std::string("Postfix ") + lexer::to_string_token(v->op)); child(v->operand);
    } else if (const auto* v = dynamic_cast<const CastExpr*>(&n)) {
        label(std::string(v->ambiguous ? "CastOrGrouping " : "Cast ") + type_name(v->type)); child(v->value);
    } else if (const auto* v = dynamic_cast<const NewExpr*>(&n)) {
        label("New " + type_name(v->type)); for (const auto& a : v->arguments) child(a);
    } else if (const auto* v = dynamic_cast<const FormattedExpr*>(&n)) {
        label("FormattedString"); for (const auto& part : v->parts) child(part);
    } else if (const auto* v = dynamic_cast<const CollectionExpr*>(&n)) {
        const char* names[] = {"ArrayLiteral", "VectorLiteral", "TupleLiteral", "DictionaryLiteral", "EmptyBraces"};
        label(names[static_cast<unsigned>(v->kind)]); for (const auto& e : v->elements) child(e);
    } else if (const auto* v = dynamic_cast<const ForStmt*>(&n)) {
        label("For"); child(v->initialization); child(v->condition);
        for (const auto& u : v->updates) child(u);
        child(v->body);
    } else if (const auto* v = dynamic_cast<const DeclGroup*>(&n)) {
        label("Declarations"); for (const auto& d : v->declarations) child(d);
    } else if (const auto* v = dynamic_cast<const AliasDecl*>(&n)) {
        label("Alias " + v->name + " = " + type_name(*v->type_target));
    } else if (const auto* v = dynamic_cast<const RecordDecl*>(&n)) {
        label(std::string(v->is_class ? "Class " : "Struct ") + v->name);
        for (const auto& m : v->members) {
            if (!m.visibility.empty()) out << std::string((depth + 1) * 2, ' ') << "Visibility " << m.visibility << '\n';
            child(m.declaration);
        }
    } else if (const auto* v = dynamic_cast<const TryStmt*>(&n)) {
        label("Try"); child(v->body);
        for (const auto& h : v->handlers) {
            out << std::string((depth + 1) * 2, ' ') << "Catch " << type_name(h.type) << " " << h.name << '\n'; child(h.body);
        }
    } else if (const auto* v = dynamic_cast<const ThrowStmt*>(&n)) {
        label("Throw"); child(v->value);
    } else if (const auto* v = dynamic_cast<const Block*>(&n)) {
        label("Block"); for (const auto& s : v->statements) child(s);
    } else if (const auto* v = dynamic_cast<const VariableDecl*>(&n)) {
        label("Variable " + v->name + ": " + type_name(v->type)); child(v->initializer);
    } else if (const auto* v = dynamic_cast<const FunctionDecl*>(&n)) {
        label(std::string(v->constructor ? "Constructor " : "Function ") + v->name + (v->return_type ? " -> " + type_name(*v->return_type) : ""));
        for (const auto& p : v->parameters)
            out << std::string((depth + 1) * 2, ' ') << "Parameter " << p.name << ": " << (p.type ? type_name(*p.type) : "<inferred>") << '\n';
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
