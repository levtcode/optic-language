#include "compiler/parser/parser.hpp"
#include "compiler/ast/dump.hpp"
#include "compiler/lexer/lexer.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
using namespace optic::compiler;
namespace {
void check(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Session {
    driver::DiagnosticsEngine diagnostics;
    driver::Module module{"test.optic"};
    std::unique_ptr<ast::ModuleAST> tree;
    explicit Session(const std::string& source) {
        module.set_buffer(source);
        lexer::Lexer(module, diagnostics).tokenize_layout(module.get_tokens());
        if (!diagnostics.has_errors()) tree = parser::Parser(7, module, diagnostics).parse();
    }
};
void valid(const std::string& source) {
    Session session(source);
    if (session.diagnostics.has_errors()) session.diagnostics.show_all();
    check(session.tree != nullptr, "valid program rejected");
}
void invalid(const std::string& source) {
    Session session(source);
    check(session.diagnostics.has_errors() && !session.tree, "invalid program accepted or partial AST published");
}
void expressions() {
    Session s("int value = a + b * 3\nx = y = 2\n");
    check(s.tree && !s.diagnostics.has_errors(), "expression parse failed");
    auto* decl = dynamic_cast<ast::VariableDecl*>(s.tree->statements[0].get());
    check(decl && decl->name == "value" && decl->type.name == "int", "variable representation");
    auto* add = dynamic_cast<ast::BinaryExpr*>(decl->initializer.get());
    check(add && add->op == lexer::TokenType::Plus, "addition precedence");
    auto* mul = dynamic_cast<ast::BinaryExpr*>(add->right.get());
    check(mul && mul->op == lexer::TokenType::Star, "multiplication precedence");
    auto* expr = dynamic_cast<ast::ExprStmt*>(s.tree->statements[1].get());
    auto* assign = dynamic_cast<ast::AssignExpr*>(expr->expression.get());
    check(assign && dynamic_cast<ast::AssignExpr*>(assign->value.get()), "right associative assignment");
    Session left("a - b - c");
    auto* expression = dynamic_cast<ast::ExprStmt*>(left.tree->statements[0].get());
    auto* subtract = dynamic_cast<ast::BinaryExpr*>(expression->expression.get());
    check(subtract && dynamic_cast<ast::BinaryExpr*>(subtract->left.get()), "left associative subtraction");
    Session locations("string text = \"\\u0041\"\n");
    auto* var = dynamic_cast<ast::VariableDecl*>(locations.tree->statements[0].get());
    auto* literal = dynamic_cast<ast::LiteralExpr*>(var->initializer.get());
    check(literal && literal->value == "A", "literal decoding");
    check(literal->span.module == 7 && literal->span.begin == 14 && literal->span.end == 22,
          "original byte range must survive decoding");
    Session raw("r\"\\n\"");
    auto* raw_stmt = dynamic_cast<ast::ExprStmt*>(raw.tree->statements[0].get());
    check(raw_stmt->expression->span.begin == 0 && raw_stmt->expression->span.end == 5, "raw string range");
}
void blocks() {
    Session braces("function f(n: int) -> int {\n if n > 0 { return n } else { return 0 }\n}\n");
    Session layout("function f(n: int) -> int:\n    if n > 0:\n        return n\n    else:\n        return 0\n");
    check(braces.tree && layout.tree, "equivalent block styles failed");
    auto verify = [](const ast::ModuleAST& tree) {
        auto* function = dynamic_cast<ast::FunctionDecl*>(tree.statements[0].get());
        check(function && function->parameters.size() == 1 && function->return_type->name == "int", "function signature");
        auto* branch = dynamic_cast<ast::IfStmt*>(function->body->statements[0].get());
        check(branch && branch->else_branch && branch->then_branch->statements.size() == 1, "if block representation");
    };
    verify(*braces.tree); verify(*layout.tree);
    valid("function f():\n    while true {\n        if false:\n            break\n        else:\n            continue\n    }\n    return\n");
    valid("function f() {\n    if true:\n        print(1)\n    elif false { print(2) } else:\n        print(3)\n}\n");
    valid("function f(string name, n: int) { print(name, n); return 0 }");
    valid("function f():\r\n  // ignored\r\n  if true:\r\n    /* comment */\r\n    return 1\r\n");
    valid("function f() {}\n{}");
    valid("function f():\n    if (\n        true\n):\n        return 1\n    return 0\n");
    // Every node remains valid after the temporary Parser is destroyed.
    std::ostringstream output;
    ast::dump(*layout.tree, output);
    check(output.str().find("Function f -> int") != std::string::npos, "persistent AST dump");
}
void separators() {
    valid("int a = 1; int b = 2\na = a +\n  b\nprint(\n a,\n b\n)\n");
    valid("function f() { return\n  42\n}");
    Session s("function f() { return\n  42\n}");
    auto* f = dynamic_cast<ast::FunctionDecl*>(s.tree->statements[0].get());
    check(f->body->statements.size() == 2 && !dynamic_cast<ast::ReturnStmt*>(f->body->statements[0].get())->value,
          "return newline must terminate the return");
    invalid("print(1) return 0");
    invalid("int x = 1 int y = 2");
}
void errors_and_limits() {
    for (const auto& source : {"function f() {", "function () {}", "function f(x:) {}",
        "if true:\nprint(1)", "if true: print(1)", "if true:\n    x\n  y\n",
        "if true:\n\tx\n", "if true:\n    ;\n", "function f():", "else {}", "x =", "1 = 2",
        "print(1,)", "(1 + 2", "f(1", "if true { return 1", "int x = f\"hello\"",
        "for x {}", "array[int] x", "a[0]", "x++", "2 ** 3", "function f() { function g() {} }"}) invalid(source);
    Session multiple("int = 1\nprint(\n");
    check(!multiple.tree && multiple.diagnostics.error_count() >= 2, "error recovery must report independent errors");
    invalid(std::string(1000, '(') + "1" + std::string(1000, ')'));
    invalid(std::string(1000, '-') + "1");
    std::string chain = "a";
    for (int i = 0; i < 1000; ++i) chain += "+a";
    invalid(chain);
    driver::Module module("empty-stream.optic");
    driver::DiagnosticsEngine diagnostics;
    check(!parser::Parser(1, module, diagnostics).parse() && diagnostics.has_errors(), "empty stream precondition");
    const std::string program = "function f(n: int):\n    if n > 0:\n        return f(n - 1)\n    return 0\n";
    for (std::size_t end = 0; end <= program.size(); ++end) Session truncated(program.substr(0, end));
    std::mt19937 random(0x0f71c);
    const std::string alphabet = "abc123(){}[]:;,+-*/=\n \"";
    for (int sample = 0; sample < 1000; ++sample) {
        std::string source;
        for (unsigned n = random() % 150; n > 0; --n) source += alphabet[random() % alphabet.size()];
        Session fuzz(source);
    }
}
}
int main() { expressions(); blocks(); separators(); errors_and_limits(); }
