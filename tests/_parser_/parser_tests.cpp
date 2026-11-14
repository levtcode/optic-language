#include "compiler/ast/dump.hpp"
#include "compiler/driver/compiler_instance.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/preprocessor/preprocessor.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
using namespace optic::compiler;

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct Session {
    driver::DiagnosticsEngine diagnostics;
    driver::Module module{"test.optic"};
    std::unique_ptr<ast::ModuleAST> tree;

    explicit Session(const std::string& source) {
        module.set_buffer(source);
        lexer::Lexer(module, diagnostics).tokenize_layout(module.get_tokens());
        if (!diagnostics.has_errors()) {
            tree = parser::Parser(7, module, diagnostics).parse();
        }
    }
};

void valid(const std::string& source) {
    Session session(source);
    if (session.diagnostics.has_errors()) {
        session.diagnostics.show_all();
    }
    check(session.tree != nullptr, "valid program rejected");
}

void invalid(const std::string& source) {
    Session session(source);
    check(session.diagnostics.has_errors() && !session.tree,
          "invalid program accepted or partial AST published");
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
    check(assign && dynamic_cast<ast::AssignExpr*>(assign->value.get()),
          "right associative assignment");
    Session left("a - b - c");
    auto* expression = dynamic_cast<ast::ExprStmt*>(left.tree->statements[0].get());
    auto* subtract = dynamic_cast<ast::BinaryExpr*>(expression->expression.get());
    check(subtract && dynamic_cast<ast::BinaryExpr*>(subtract->left.get()),
          "left associative subtraction");
    Session locations("string text = \"\\u0041\"\n");
    auto* var = dynamic_cast<ast::VariableDecl*>(locations.tree->statements[0].get());
    auto* literal = dynamic_cast<ast::LiteralExpr*>(var->initializer.get());
    check(literal && literal->value == "A", "literal decoding");
    check(literal->span.module == 7 && literal->span.begin == 14 && literal->span.end == 22,
          "original byte range must survive decoding");
    Session raw("r\"\\n\"");
    auto* raw_stmt = dynamic_cast<ast::ExprStmt*>(raw.tree->statements[0].get());
    check(raw_stmt->expression->span.begin == 0 && raw_stmt->expression->span.end == 5,
          "raw string range");
}

void blocks() {
    Session braces("function f(n: int) -> int {\n if n > 0 { return n } else { return 0 }\n}\n");
    check(braces.tree != nullptr, "brace block parse failed");
    auto* f = dynamic_cast<ast::FunctionDecl*>(braces.tree->statements[0].get());
    check(f && f->parameters[0].type->name == "int", "annotated function signature");
    check(dynamic_cast<ast::IfStmt*>(f->body->statements[0].get())->else_branch != nullptr,
          "else branch");
    valid("function f() {\n\twhile true { if false { break } else { continue } }\n return\n}\n");
    valid("function f(name: string, n) { print(name, n); return 0 }");
    valid("function f()\r\n{\r\n // comment\r\n return 1\r\n}\r\n");
    valid("function f() {}\n{}");
    invalid("function f():\n    return 1\n");
    std::ostringstream output;
    ast::dump(*braces.tree, output);
    check(output.str().find("Function f -> int") != std::string::npos, "persistent AST dump");
}

void types_and_containers() {
    Session s("alias [int | float | double | string] as numeric\nfunction add(a: numeric, b) -> "
              "numeric { return a + b }\narray[array[int, 2], 3] matrix = {{1,2},{3,4},{5,6}}\n");
    check(s.tree != nullptr, "type expressions and restrictions");
    auto* alias = dynamic_cast<ast::AliasDecl*>(s.tree->statements[0].get());
    check(alias && alias->type_target->alternatives.size() == 4,
          "alias must retain one union target");
    auto* f = dynamic_cast<ast::FunctionDecl*>(s.tree->statements[1].get());
    check(f && !f->parameters[1].type && f->parameters[0].type->name == "numeric",
          "implicit static generic parameter");
    auto* matrix = dynamic_cast<ast::VariableDecl*>(s.tree->statements[2].get());
    check(matrix && matrix->type.arguments.size() == 2 &&
              matrix->type.arguments[0].arguments.size() == 2,
          "nested type application");
    valid("array[int, x + 1] sized\n");
    valid("array[int, x] arr\narr[-1] = i\narr.at(2) = 4\nvector[int] *p\ndelete p\n");
    valid("dict[string, dict[string, int]] people = {\n \"angel\": { \"edad\": 15 },\n}\n");
    valid("tuple[int, string, bool] tup = (1, \"hola\", true)\nvector v = [1,2,3,]\n");
    valid("const size_t bytes = 1024\nint x = 5, y = 2\nalias [unsigned long] as size_t\n");
    valid("function main() { for (int i = 0; i < x; i++) { arr[i] = i } }\n");
    valid("for (; ptr < &array1[-1]; ptr++) {}\nfor (;;) { break }\n");
    valid("struct Tree { int id\n Tree *next }\nTree *root = new Tree()\n");
    valid("class Book { public string name\nprivate int stock\nBook(name) { self.name = name "
          "}\npublic function add(n: int) -> void { self.stock += n }\n}\n");
    valid("try { read(system::stdin) } catch (system::stream_exception &exception) { throw "
          "exception }\n");
}

void casts_and_strings() {
    Session s("result = (int) (a + b)\n");
    check(s.tree != nullptr, "explicit cast");
    auto* assignment = dynamic_cast<ast::AssignExpr*>(
        dynamic_cast<ast::ExprStmt*>(s.tree->statements[0].get())->expression.get());
    check(dynamic_cast<ast::CastExpr*>(assignment->value.get())->type.name == "int",
          "cast type structure");
    valid("string a = \"hello\"\n    \" world\"\nstring b = r\"raw\nmultiline\"\n");
    valid("string b = \"hello \\\n world\"\n");
    Session formatted("text = f\"value: {23}, {x + 1}\"\n");
    check(formatted.tree != nullptr, "formatted expression");
    auto* value = dynamic_cast<ast::AssignExpr*>(
        dynamic_cast<ast::ExprStmt*>(formatted.tree->statements[0].get())->expression.get());
    auto* string = dynamic_cast<ast::FormattedExpr*>(value->value.get());
    check(string && string->parts.size() == 4, "formatted string parts");
    check(string->parts[1]->span.begin == 17 && string->parts[1]->span.line == 1,
          "embedded expression original span");
    valid("f\"{{value}} {call(\"nested\")}\"\n");
    invalid("f\"value: {}\"");
    invalid("f\"value: {1 +}\"");
}

void separators() {
    valid("int a = 1; int b = 2\na = a +\n  b\nprint(\n a,\n b\n)\n");
    valid("function f() { return\n  42\n}");
    Session s("function f() { return\n  42\n}");
    auto* f = dynamic_cast<ast::FunctionDecl*>(s.tree->statements[0].get());
    check(f->body->statements.size() == 2 &&
              !dynamic_cast<ast::ReturnStmt*>(f->body->statements[0].get())->value,
          "return newline must terminate the return");
    invalid("print(1) return 0");
    invalid("int x = 1 int y = 2");
}

void ambiguity_and_imports() {
    valid("a * b + c\na & b | c\nscope::a * b + c\na * b(2)\n");
    Session ambiguous("a * b\nvalue = (Unknown) + x * y\n");
    check(ambiguous.tree != nullptr, "unresolved syntax must survive parsing");
    check(dynamic_cast<ast::AmbiguousStmt*>(ambiguous.tree->statements[0].get()),
          "pointer declaration vs product must remain unresolved");
    auto* whole = dynamic_cast<ast::UnresolvedExpr*>(
        dynamic_cast<ast::ExprStmt*>(ambiguous.tree->statements[1].get())->expression.get());
    check(whole && !whole->tokens.empty(),
          "cast/grouping ambiguity must retain the whole expression");
    const auto root = std::filesystem::temp_directory_path() /
                      ("optic-parser-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);

    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{root};

    std::ofstream(root / "system.optic") << "function input() {}\n";
    std::ofstream(root / "utils.optic") << "function check() {}\n";
    std::ofstream(root / "main.optic")
        << "import system as os, utils\nfunction main() { os::input() }\n";
    driver::CompilerContext context;
    preprocessor::Preprocessor preprocessor(context);
    check(preprocessor.run({(root / "main.optic").string()}), "aliased multiple import failed");
    const auto id = context.modules().find_module(root / "main.optic");
    const auto* module = context.modules().get_module(id);
    check(module && module->get_imports().size() == 2, "import bindings lost");
    check(module->get_imports()[0].alias == "os" && module->get_imports()[0].name == "system",
          "alias binding identity");
    check(module->get_imports()[0].target == context.modules().find_module(root / "system.optic"),
          "alias points at wrong module");
}

void errors_and_limits() {
    for (const auto& source : {"function f() {",
                               "function () {}",
                               "function f(x:) {}",
                               "if true:\nprint(1)",
                               "if true: print(1)",
                               "function f():",
                               "else {}",
                               "x =",
                               "1 = 2",
                               "(1 + 2",
                               "f(1",
                               "if true { return 1",
                               "for x {}",
                               "array<int,4> x",
                               "int arr[4]",
                               "for (i=0, i<n, i++) {}",
                               "alias \"int | string\" as Params",
                               "alias int | string as Params",
                               "alias [1 + 2] as Three",
                               "alias [int | ] as Params",
                               "dict[string, int] d = {\"a\" 1}",
                               "2 ** 3",
                               "function f() { function g() {} }"}) {
        invalid(source);
    }
    Session multiple("int = 1\nprint(\n");
    check(!multiple.tree && multiple.diagnostics.error_count() >= 2,
          "error recovery must report independent errors");
    invalid(std::string(1000, '(') + "1" + std::string(1000, ')'));
    invalid(std::string(1000, '-') + "1");
    std::string chain = "a";
    for (int i = 0; i < 1000; ++i) {
        chain += "+a";
    }
    invalid(chain);
    driver::Module module("empty-stream.optic");
    driver::DiagnosticsEngine diagnostics;
    check(!parser::Parser(1, module, diagnostics).parse() && diagnostics.has_errors(),
          "empty stream precondition");
    const std::string program =
        "function f(n: int) {\n if n > 0 { return f(n - 1) }\n return 0\n}\n";
    for (std::size_t end = 0; end <= program.size(); ++end) {
        Session truncated(program.substr(0, end));
    }
    std::mt19937 random(0x0f71c);
    const std::string alphabet = "abc123(){}[]:;,+-*/=\n \"";
    for (int sample = 0; sample < 1000; ++sample) {
        std::string source;
        for (unsigned n = random() % 150; n > 0; --n) {
            source += alphabet[random() % alphabet.size()];
        }
        Session fuzz(source);
    }
}
} // namespace

int main() {
    expressions();
    blocks();
    types_and_containers();
    casts_and_strings();
    separators();
    ambiguity_and_imports();
    errors_and_limits();
}
