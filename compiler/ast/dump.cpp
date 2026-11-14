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
        if (const auto* literal = dynamic_cast<const LiteralExpr*>(type.value_argument.get())) {
            name += literal->value;
        } else if (const auto* symbol = dynamic_cast<const NameExpr*>(type.value_argument.get())) {
            name += symbol->name;
        } else {
            name += "<expression>";
        }
    }
    if (!type.arguments.empty()) {
        name += "[";
        for (const auto& argument : type.arguments) {
            if (name.back() != '[') {
                name += ", ";
            }
            name += type_name(argument);
        }
        name += "]";
    }
    for (const auto& alternative : type.alternatives) {
        if (!name.empty()) {
            name += " | ";
        }
        name += type_name(alternative);
    }
    name += std::string(type.pointers, '*');
    if (type.reference) {
        name += "&";
    }
    return name;
}

void node(const Node& syntax_node, std::ostream& out, unsigned depth) {
    out << std::string(depth * 2, ' ');
    auto child = [&](const auto& p) {
        if (p) {
            node(*p, out, depth + 1);
        }
    };
    auto label = [&](const std::string& text) {
        out << text << " @" << syntax_node.span.line << ':' << syntax_node.span.column << '\n';
    };
    if (const auto* name = dynamic_cast<const NameExpr*>(&syntax_node)) {
        label("Name " + name->name);
    } else if (const auto* literal = dynamic_cast<const LiteralExpr*>(&syntax_node)) {
        out << "Literal " << lexer::to_string_token(literal->kind) << ' '
            << std::quoted(literal->value) << " @" << syntax_node.span.line << ':'
            << syntax_node.span.column << '\n';
    } else if (const auto* unary = dynamic_cast<const UnaryExpr*>(&syntax_node)) {
        label(std::string("Unary ") + lexer::to_string_token(unary->op));
        child(unary->operand);
    } else if (const auto* binary = dynamic_cast<const BinaryExpr*>(&syntax_node)) {
        label(std::string("Binary ") + lexer::to_string_token(binary->op));
        child(binary->left);
        child(binary->right);
    } else if (const auto* assignment = dynamic_cast<const AssignExpr*>(&syntax_node)) {
        label(std::string("Assign ") + lexer::to_string_token(assignment->op));
        child(assignment->target);
        child(assignment->value);
    } else if (const auto* member_access = dynamic_cast<const MemberExpr*>(&syntax_node)) {
        label(std::string(member_access->qualified ? "Qualified " : "Member ") +
              member_access->member);
        child(member_access->object);
    } else if (const auto* call = dynamic_cast<const CallExpr*>(&syntax_node)) {
        label("Call");
        child(call->callee);
        for (const auto& a : call->arguments) {
            child(a);
        }
    } else if (const auto* ambiguous_statement = dynamic_cast<const AmbiguousStmt*>(&syntax_node)) {
        label("DeclarationOrExpression");
        child(ambiguous_statement->declaration);
        child(ambiguous_statement->expression);
    } else if (const auto* unresolved_expression =
                   dynamic_cast<const UnresolvedExpr*>(&syntax_node)) {
        label("UnresolvedExpression");
        child(unresolved_expression->provisional);
    } else if (const auto* index = dynamic_cast<const IndexExpr*>(&syntax_node)) {
        label("Index");
        child(index->object);
        child(index->index);
    } else if (const auto* postfix = dynamic_cast<const PostfixExpr*>(&syntax_node)) {
        label(std::string("Postfix ") + lexer::to_string_token(postfix->op));
        child(postfix->operand);
    } else if (const auto* cast = dynamic_cast<const CastExpr*>(&syntax_node)) {
        label(std::string(cast->ambiguous ? "CastOrGrouping " : "Cast ") + type_name(cast->type));
        child(cast->value);
    } else if (const auto* allocation = dynamic_cast<const NewExpr*>(&syntax_node)) {
        label("New " + type_name(allocation->type));
        for (const auto& a : allocation->arguments) {
            child(a);
        }
    } else if (const auto* formatted_string = dynamic_cast<const FormattedExpr*>(&syntax_node)) {
        label("FormattedString");
        for (const auto& part : formatted_string->parts) {
            child(part);
        }
    } else if (const auto* collection = dynamic_cast<const CollectionExpr*>(&syntax_node)) {
        const char* names[] = {
            "ArrayLiteral", "VectorLiteral", "TupleLiteral", "DictionaryLiteral", "EmptyBraces"};
        label(names[static_cast<unsigned>(collection->kind)]);
        for (const auto& e : collection->elements) {
            child(e);
        }
    } else if (const auto* loop = dynamic_cast<const ForStmt*>(&syntax_node)) {
        label("For");
        child(loop->initialization);
        child(loop->condition);
        for (const auto& u : loop->updates) {
            child(u);
        }
        child(loop->body);
    } else if (const auto* declaration_group = dynamic_cast<const DeclGroup*>(&syntax_node)) {
        label("Declarations");
        for (const auto& d : declaration_group->declarations) {
            child(d);
        }
    } else if (const auto* alias = dynamic_cast<const AliasDecl*>(&syntax_node)) {
        label("Alias " + alias->name + " = " + type_name(*alias->type_target));
    } else if (const auto* record = dynamic_cast<const RecordDecl*>(&syntax_node)) {
        label(std::string(record->is_class ? "Class " : "Struct ") + record->name);
        for (const auto& m : record->members) {
            if (!m.visibility.empty()) {
                out << std::string((depth + 1) * 2, ' ') << "Visibility " << m.visibility << '\n';
            }
            child(m.declaration);
        }
    } else if (const auto* try_statement = dynamic_cast<const TryStmt*>(&syntax_node)) {
        label("Try");
        child(try_statement->body);
        for (const auto& h : try_statement->handlers) {
            out << std::string((depth + 1) * 2, ' ') << "Catch " << type_name(h.type) << " "
                << h.name << '\n';
            child(h.body);
        }
    } else if (const auto* throw_statement = dynamic_cast<const ThrowStmt*>(&syntax_node)) {
        label("Throw");
        child(throw_statement->value);
    } else if (const auto* block = dynamic_cast<const Block*>(&syntax_node)) {
        label("Block");
        for (const auto& s : block->statements) {
            child(s);
        }
    } else if (const auto* variable = dynamic_cast<const VariableDecl*>(&syntax_node)) {
        label("Variable " + variable->name + ": " + type_name(variable->type));
        child(variable->initializer);
    } else if (const auto* function = dynamic_cast<const FunctionDecl*>(&syntax_node)) {
        label(std::string(function->constructor ? "Constructor " : "Function ") + function->name +
              (function->return_type ? " -> " + type_name(*function->return_type) : ""));
        for (const auto& p : function->parameters) {
            out << std::string((depth + 1) * 2, ' ') << "Parameter " << p.name << ": "
                << (p.type ? type_name(*p.type) : "<inferred>") << '\n';
        }
        child(function->body);
    } else if (const auto* if_statement = dynamic_cast<const IfStmt*>(&syntax_node)) {
        label("If");
        child(if_statement->condition);
        child(if_statement->then_branch);
        child(if_statement->else_branch);
    } else if (const auto* loop = dynamic_cast<const WhileStmt*>(&syntax_node)) {
        label("While");
        child(loop->condition);
        child(loop->body);
    } else if (const auto* return_statement = dynamic_cast<const ReturnStmt*>(&syntax_node)) {
        label("Return");
        child(return_statement->value);
    } else if (const auto* expression_statement = dynamic_cast<const ExprStmt*>(&syntax_node)) {
        label("Expression");
        child(expression_statement->expression);
    } else if (dynamic_cast<const BreakStmt*>(&syntax_node)) {
        label("Break");
    } else if (dynamic_cast<const ContinueStmt*>(&syntax_node)) {
        label("Continue");
    }
}
} // namespace

void dump(const ModuleAST& module, std::ostream& out) {
    out << "ModuleAST [" << module.module << "]\n";
    for (const auto& statement : module.statements) {
        node(*statement, out, 1);
    }
}
} // namespace optic::compiler::ast
