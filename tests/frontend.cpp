/** @file frontend.cpp
 * @brief Regression checks for module identity, graph invariants and lexical locations.
 */
#include "compiler/driver/module.hpp"
#include "compiler/lexer/lexer.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>

using namespace optic::compiler;

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void graph_checks() {
    driver::ModuleGraph graph;
    check(graph.add_module(nullptr) == driver::invalid_module_id, "null module accepted");
    const auto a = graph.add_module(std::make_unique<driver::Module>("a.optic"));
    const auto b = graph.add_module(std::make_unique<driver::Module>("b.optic"));
    const auto c = graph.add_module(std::make_unique<driver::Module>("c.optic"));
    check(graph.add_module(std::make_unique<driver::Module>("./a.optic")) == a,
          "path alias duplicated");
    check(graph.size() == 3, "incorrect module count");
    check(graph.get_module(0) == nullptr && graph.get_module(999) == nullptr,
          "invalid lookup accepted");
    check(!graph.add_dependency(a, 999), "invalid edge accepted");
    check(graph.add_dependency(a, b) && graph.add_dependency(b, c), "valid edge rejected");
    check(graph.add_dependency(a, b), "duplicate edge rejected");
    check(graph.get_module(a)->get_dependencies().size() == 1, "duplicate dependency stored");
    check(!graph.add_dependency(c, a) && !graph.add_dependency(a, a), "cycle accepted");
    check(graph.topological_sort(), "valid graph failed sorting");
    const std::vector<driver::ModuleID> expected{c, b, a};
    check(graph.compilation_order() == expected, "dependencies must precede importers");
    check(graph.topological_sort() && graph.compilation_order() == expected,
          "sort is not idempotent");
    const auto& const_graph = graph;
    check(const_graph.get_module(b) != nullptr, "const lookup failed");
    check(const_graph.find_module("./b.optic") == b, "canonical lookup failed");
}

void lexer_checks() {
    driver::DiagnosticsEngine diagnostics;
    driver::Module module("positions.optic");
    module.set_buffer("name_2/2 /= 3\r\n\"\" \"/ import x /* */\" '\\n'\n+4-5 >>==6 && !false ? "
                      "1:0 // tail\n/* end */");
    lexer::Lexer lexer(module, diagnostics);
    auto& tokens = module.get_tokens();
    lexer.tokenize(tokens);
    check(!diagnostics.has_errors(), "valid source produced errors");
    check(tokens[0].value == "name_2" && tokens[0].line == 1 && tokens[0].column == 1,
          "identifier location");
    check(tokens[1].type == lexer::TokenType::Slash && tokens[1].column == 7,
          "division or location lost");
    check(tokens[3].type == lexer::TokenType::DivAssign, "compound division lost");
    check(tokens[5].value.empty() && tokens[5].line == 2 && tokens[5].column == 1,
          "empty string lost");
    check(tokens[6].value == "/ import x /* */", "string interpreted as preprocessing syntax");
    check(tokens[7].type == lexer::TokenType::CharLiteral && tokens[7].value == "\n",
          "first escape in literal");
    check(tokens[8].type == lexer::TokenType::Plus && tokens[9].value == "4",
          "unary sign merged with number");
    check(tokens[12].type == lexer::TokenType::RShiftAssign &&
              tokens[13].type == lexer::TokenType::Equal,
          "longest operator match lost or duplicated bytes");
    check(tokens.back().type == lexer::TokenType::EndOfFile && tokens.back().line == 4,
          "EOF location");
    const auto count = tokens.size();
    lexer.tokenize(tokens);
    check(tokens.size() == count && tokens[0].column == 1, "tokenization cannot be repeated");

    module.set_buffer("\"\\x41\\101\\u03A9\\U0001F600\" r\"\\n\" f\"hello\"");
    lexer.tokenize(tokens);
    check(tokens[0].value == "AA\xCE\xA9\xF0\x9F\x98\x80", "escape decoding failed");
    check(tokens[1].type == lexer::TokenType::RawString && tokens[2].value == "\\n",
          "raw string changed");
    check(tokens[3].type == lexer::TokenType::FString && tokens[4].value == "hello",
          "formatted string prefix lost");
    module.set_buffer("int value = 1; float other = 1.5; string text = \"text\"; char c = 'x';");
    lexer.tokenize(tokens);
    check(tokens[0].type == lexer::TokenType::Int &&
              tokens[3].type == lexer::TokenType::IntegerLiteral,
          "type keyword and integer literal must be distinct");
    check(tokens[5].type == lexer::TokenType::Float &&
              tokens[8].type == lexer::TokenType::FloatLiteral,
          "type keyword and floating literal must be distinct");
    check(tokens[10].type == lexer::TokenType::String &&
              tokens[13].type == lexer::TokenType::StringLiteral,
          "type keyword and string literal must be distinct");
    check(tokens[15].type == lexer::TokenType::Char &&
              tokens[18].type == lexer::TokenType::CharLiteral,
          "type keyword and character literal must be distinct");
    for (const auto& source : {"/*", "/*x*", "\"", "\"\\x\"", "\"\\uD800\"", "\"\\777\"", "@"}) {
        driver::DiagnosticsEngine invalid;
        module.set_buffer(source);
        lexer::Lexer(module, invalid).tokenize(tokens);
        check(invalid.has_errors(), "malformed input silently accepted");
        check(tokens.back().type == lexer::TokenType::EndOfFile, "malformed input lost EOF");
    }
    // Truncated/random byte streams previously triggered out-of-bounds reads and
    // numeric conversion exceptions. Sanitizers exercise every reader here.
    std::mt19937 random(0x0f71c);
    for (int sample = 0; sample < 2000; ++sample) {
        std::string bytes;
        const auto length = random() % 129;
        for (unsigned i = 0; i < length; ++i) {
            bytes += static_cast<char>(random() % 256);
        }
        driver::DiagnosticsEngine fuzz_diagnostics;
        module.set_buffer(bytes);
        lexer::Lexer(module, fuzz_diagnostics).tokenize(tokens);
        check(!tokens.empty() && tokens.back().type == lexer::TokenType::EndOfFile,
              "random byte stream lost EOF");
        check(tokens.size() <= bytes.size() + 1, "lexer made no progress");
        for (const auto& token : tokens) {
            check(token.line >= 1 && token.column >= 1, "invalid source position");
        }
    }
}

void token_dump_checks() {
    driver::Module module("sample.optic");
    module.get_tokens() = {
        {lexer::TokenType::LeftParen, "(", 1, 14},
        {lexer::TokenType::RightParen, ")", 1, 15},
        {lexer::TokenType::LeftBrace, "{", 1, 17},
        {lexer::TokenType::Newline, "", 1, 18},
        {lexer::TokenType::EndOfFile, "EOF", 2, 1},
    };
    std::ostringstream output;
    lexer::Lexer::print_tokens(module, output);
    check(output.str() == "Tokens - sample.optic\n\n"
                          "  Line:Col  Type        Representation\n"
                          "  --------  ----------  --------------\n"
                          "      1:14  LeftParen   \"(\"\n"
                          "      1:15  RightParen  \")\"\n"
                          "      1:17  LeftBrace   \"{\"\n"
                          "      1:18  Newline     \"\\n\"\n"
                          "       2:1  EndOfFile   —\n\n",
          "compact token table layout");
    check(module.get_tokens()[3].value.empty() && module.get_tokens()[4].value == "EOF",
          "presentation must not modify token values");

    std::string controls = "\"\\\n\r\t";
    controls += '\0';
    controls += '\x1b';
    controls += " Ω";
    module.get_tokens() = {{lexer::TokenType::StringLiteral, controls, 123456789, 123456789},
                           {lexer::TokenType::StringLiteral, "", 2, 1}};
    output.str("");
    output.clear();
    lexer::Lexer::print_tokens(module, output);
    check(output.str().find(
              "123456789:123456789  StringLiteral  \"\\\"\\\\\\n\\r\\t\\x00\\x1b Ω\"") !=
              std::string::npos,
          "escape controls, preserve UTF-8 and support wide source positions");
    check(output.str().find("2:1  StringLiteral  \"\"") != std::string::npos,
          "empty string representation");
    check(output.str().find('\x1b') == std::string::npos &&
              output.str().find('\0') == std::string::npos,
          "control bytes must not reach terminal output");

    driver::Module absolute((std::filesystem::current_path() / "sample.optic").string());
    output.str("");
    lexer::Lexer::print_tokens(absolute, output);
    check(output.str().starts_with("Tokens - sample.optic\n"), "relative display path");
}
} // namespace

int main() {
    graph_checks();
    lexer_checks();
    token_dump_checks();
}
