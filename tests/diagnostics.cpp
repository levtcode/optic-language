/** Regression checks for source diagnostics and rendering boundaries. */
#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/driver/module.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <sstream>

using namespace optic::compiler;
using namespace optic::compiler::driver;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main() {
    DiagnosticsEngine engine;
    engine.register_source("main.optic", "function main() {\n\tx = void + 1\n}\n");
    SourceLocation loc{SourceKind::BufferAST, "main.optic", "main", 2, 6};
    engine.report(loc,
                  "Error: invalid expression\n",
                  "Use a value.",
                  "Operands need values.",
                  DiagnosticsLevel::Error,
                  SourceLocationRange::from_text(loc, "void + 1"));
    const auto text = engine.render(engine.entries().back());
    check(text == "Error: invalid expression\n"
                  "  --> main.optic:2:6\n"
                  "  |\n"
                  "1 | function main() {\n"
                  "2 |     x = void + 1\n"
                  "  |         ^~~~~~~~\n"
                  "3 | }\n"
                  "  = in function: main\n"
                  "  = help: Use a value.\n"
                  "  = note: Operands need values.\n",
          "expression rendering");
    check(engine.render(engine.entries().back(), true).find("\033[31m") != std::string::npos,
          "error color missing");
    check(text.find('\033') == std::string::npos, "plain rendering contains ANSI");
    check(engine.format_msg("Error: bad", "\tx", 10, 2) ==
              "Error: bad\n   |\n10 |     x\n   |     ^\n",
          "legacy helper alignment");

    DiagnosticsEngine edges;
    edges.register_source("edge.optic", "one\r\n\ttwo\r\n\n");
    SourceLocation begin{SourceKind::BufferNoAST, "edge.optic", "", 1, 2};
    auto end = begin;
    end.line = 3;
    end.column = 1;
    edges.report(
        begin, "multiline", "", "", DiagnosticsLevel::Warning, SourceLocationRange{begin, end});
    const auto multi = edges.render(edges.entries().back());
    check(multi.find("1 | one\n  |  ^~\n2 |     two\n  | ~~~~~~~\n3 | \n") != std::string::npos,
          "multiline range or CRLF rendering");
    check(edges.render(edges.entries().back(), true).find("\033[33m") != std::string::npos,
          "warning color missing");
    begin.line = 4;
    begin.column = 1;
    edges.report(begin, "EOF", "", "", DiagnosticsLevel::Error);
    check(edges.render(edges.entries().back()).find("4 | \n  | ^\n") != std::string::npos,
          "EOF after trailing newline");
    begin.line = std::numeric_limits<unsigned>::max();
    edges.report(begin, "bad position", "", "", DiagnosticsLevel::Hint);
    check(edges.render(edges.entries().back()).find(" |") == std::string::npos,
          "invalid position rendered a false excerpt");
    edges.report({SourceKind::Stdin, "", "", 0, 0},
                 "Error: bad argument\n",
                 "",
                 "",
                 DiagnosticsLevel::Error);
    check(edges.render(edges.entries().back()) == "Error: bad argument\n  --> <stdin>\n",
          "source-free diagnostic");
    edges.register_source("empty.optic", "");
    edges.report({SourceKind::BufferNoAST, "empty.optic", "", 1, 1},
                 "empty",
                 "",
                 "",
                 DiagnosticsLevel::Error);
    check(edges.render(edges.entries().back()).find("1 | \n  | ^\n") != std::string::npos,
          "empty source");
    std::string long_source;
    for (int i = 0; i < 100; ++i) {
        long_source += "line\n";
    }
    edges.register_source("long.optic", long_source);
    begin = {SourceKind::BufferAST, "long.optic", "", 2, 1};
    end = {SourceKind::BufferAST, "long.optic", "", 99, 5};
    edges.report(
        begin, "long range", "", "", DiagnosticsLevel::Error, SourceLocationRange{begin, end});
    const auto abbreviated = edges.render(edges.entries().back());
    check(abbreviated.size() < 500 && abbreviated.find(" | ...\n") != std::string::npos &&
              abbreviated.find(" 99 | line") != std::string::npos,
          "long range not bounded");

    edges.register_source("utf8.optic", "é\tbad");
    begin = {SourceKind::BufferNoAST, "utf8.optic", "", 1, 4};
    edges.report(begin,
                 "unicode prefix",
                 "",
                 "",
                 DiagnosticsLevel::Error,
                 SourceLocationRange::from_text(begin, "bad"));
    check(edges.render(edges.entries().back()).find("1 | é   bad\n  |     ^~~\n") !=
              std::string::npos,
          "UTF-8 prefix alignment");
    end = begin;
    end.file = "other.optic";
    end.column = 6;
    edges.report(
        begin, "wrong source", "", "", DiagnosticsLevel::Error, SourceLocationRange{begin, end});
    check(edges.render(edges.entries().back()).find("  |     ^\n") != std::string::npos,
          "cross-source range must fall back to point");
    end = begin;
    end.column = 999;
    edges.report(
        begin, "invalid end", "", "", DiagnosticsLevel::Error, SourceLocationRange{begin, end});
    check(edges.render(edges.entries().back()).find("  |     ^\n") != std::string::npos,
          "out-of-bounds range must fall back to point");
    begin.column = 7;
    edges.report(begin, "EOF without newline", "", "", DiagnosticsLevel::Error);
    check(edges.render(edges.entries().back()).find("  |        ^\n") != std::string::npos,
          "EOF without newline");

    // Exercise real producers and retain the source after module destruction.
    DiagnosticsEngine frontend;
    {
        Module module("lexer.optic");
        module.set_buffer("function main() {\n\t@\n}\n");
        lexer::Lexer(module, frontend).tokenize_layout(module.get_tokens());
    }
    check(frontend.render(frontend.entries().front()).find("2 |     @\n  |     ^\n") !=
              std::string::npos,
          "lexer source not retained");
    {
        Module module("parser.optic");
        module.set_buffer("function main() {\n    int return\n}\n");
        lexer::Lexer(module, frontend).tokenize_layout(module.get_tokens());
        parser::Parser parser(1, module, frontend);
        check(!parser.parse(), "invalid declaration accepted");
    }
    check(frontend.render(frontend.entries()[1]).find("^~~~~~") != std::string::npos,
          "parser did not underline raw token");

    // Guide mode must inspect the diagnostic that show() just advanced past.
    DiagnosticsEngine guide;
    auto config = guide.get_config();
    config.color_diagnostics = false;
    guide.set_config(config);
    guide.report({}, "bad", "fix", "detail", DiagnosticsLevel::Error);
    std::istringstream input("1\n");
    std::ostringstream output;
    auto* in = std::cin.rdbuf(input.rdbuf());
    auto* out = std::cout.rdbuf(output.rdbuf());
    auto* err = std::cerr.rdbuf(output.rdbuf());
    guide.run_guide_engine();
    guide.show();
    std::cin.rdbuf(in);
    std::cout.rdbuf(out);
    std::cerr.rdbuf(err);
    check(output.str().find("fix") != std::string::npos, "guide inspected wrong diagnostic");
}
