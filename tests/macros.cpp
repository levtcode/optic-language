/** @file macros.cpp
 * @brief Macro environment transactions, module isolation and malformed-input checks.
 */
#include "compiler/driver/module.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/preprocessor/macro.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
using namespace optic::compiler;

namespace {
void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::vector<lexer::token_t> lex(const std::string& source) {
    driver::Module module("macro-test.optic");
    module.set_buffer(source);
    driver::DiagnosticsEngine diagnostics;
    std::vector<lexer::token_t> tokens;
    lexer::Lexer(module, diagnostics).tokenize(tokens);
    check(!diagnostics.has_errors(), "invalid lexical fixture");
    return tokens;
}

void define(preprocessor::MacroEnvironment& environment, const std::string& source) {
    auto tokens = lex(source);
    std::size_t cursor = 1;
    environment.define(tokens, cursor, "definitions.optic");
    check(tokens[cursor].type == lexer::TokenType::EndOfFile,
          "macro declaration left trailing tokens");
}

std::vector<lexer::token_t> expand(preprocessor::MacroEnvironment& environment,
                                   const std::string& source) {
    auto tokens = lex(source);
    std::size_t cursor = 0;
    auto output = environment.expand(tokens, cursor, "caller.optic");
    check(tokens[cursor].type == lexer::TokenType::EndOfFile,
          "macro invocation left trailing tokens");
    return output;
}

void expect_value(preprocessor::MacroEnvironment& environment,
                  const std::string& source,
                  const std::string& expected) {
    const auto tokens = expand(environment, source);
    check(tokens.size() == 1 && tokens[0].value == expected, "unexpected expanded macro value");
}

void transactions() {
    preprocessor::MacroEnvironment environment;
    define(environment, R"(macro M {
        value: 1;
        expression: ?
        fn set(int val) { value = val; }
        fn bad() -> int { value = 9; return 1 / 0; }
        fn broken() -> string { value = 7; return "@"; }
        fn to_string() -> string { return string(value); }
    })");
    expect_value(environment, "M.value", "1");
    auto bad = lex("M.bad()");
    std::size_t cursor = 0;
    bool failed = false;
    try {
        environment.expand(bad, cursor, "caller.optic");
    } catch (const preprocessor::MacroError& error) {
        failed = true;
        check(error.location.file == "definitions.optic", "method error lost definition identity");
        check(std::string(error.what()).find("caller.optic") != std::string::npos,
              "method error lost invocation identity");
    }
    check(failed && cursor == 0, "failing expansion advanced its caller or silently succeeded");
    expect_value(environment, "M.value", "1");
    failed = false;
    try {
        define(environment, "macro Bad { value: M.set(99); }");
    } catch (const preprocessor::MacroError&) {
        failed = true;
    }
    check(failed && !environment.contains("Bad"), "failed definition became visible");
    expect_value(environment, "M.value", "1");
    define(environment,
           "macro Invalid { value: 0; fn to_string() -> string { M.set(8); return \"@\"; } }");
    failed = false;
    try {
        expand(environment, "Invalid");
    } catch (const preprocessor::MacroError&) {
        failed = true;
    }
    check(failed, "invalid generated source accepted");
    expect_value(environment, "M.value", "1");

    preprocessor::MacroEnvironment importer;
    importer.import(environment, {});
    check(expand(importer, "M.set(5);").empty(), "void call leaked tokens");
    expect_value(importer, "M.value", "5");
    expect_value(environment, "M.value", "1");
    importer.import(environment, {});
    expect_value(importer, "M.value", "5");
    auto copy = importer;
    expand(copy, "M.set(6);");
    expect_value(importer, "M.value", "5");
    expect_value(copy, "M.value", "6");
    preprocessor::MacroEnvironment conflicting;
    define(conflicting, "macro M { value: 2; }");
    define(conflicting, "macro A { value: 3; }");
    failed = false;
    try {
        importer.import(conflicting, {});
    } catch (const preprocessor::MacroError&) {
        failed = true;
    }
    check(failed && !importer.contains("A"), "conflicting import partially changed environment");
    expect_value(importer, "M.value", "5");
}

void malformed_input() {
    const std::string declaration = R"(macro Truncated {
        value: -1;
        expression: "return "
        fn set(int n) { value = n; }
        fn to_string() -> string { return f"{expression}{value};"; }
    })";
    const auto tokens = lex(declaration);
    for (std::size_t count = 1; count < tokens.size(); ++count) {
        auto truncated = std::vector<lexer::token_t>(tokens.begin(), tokens.begin() + count);
        truncated.emplace_back(lexer::TokenType::EndOfFile, "EOF", 99, 1);
        preprocessor::MacroEnvironment environment;
        std::size_t cursor = 1;
        try {
            environment.define(truncated, cursor, "truncated.optic");
        } catch (const preprocessor::MacroError&) {
            check(cursor == 1 && !environment.contains("Truncated"),
                  "truncation violated transaction");
        }
    }
    std::mt19937 random(0xabc123);
    for (int sample = 0; sample < 500; ++sample) {
        auto mutated = tokens;
        const auto index = 1 + random() % (mutated.size() - 2);
        mutated[index] = tokens[random() % (tokens.size() - 1)];
        preprocessor::MacroEnvironment environment;
        std::size_t cursor = 1;
        try {
            environment.define(mutated, cursor, "mutated.optic");
        } catch (const preprocessor::MacroError&) {
            check(cursor == 1, "mutation advanced caller on error");
        }
    }
    for (const auto& source :
         {"macro Deep { value: " + std::string(256, '(') + "1" + std::string(256, ')') + "; }",
          "macro Deep { value: 1; fn run() {" + std::string(256, '{') + std::string(256, '}') +
              "} }"}) {
        preprocessor::MacroEnvironment environment;
        bool failed = false;
        try {
            define(environment, source);
        } catch (const preprocessor::MacroError&) {
            failed = true;
        }
        check(failed, "excessive AST nesting accepted");
    }
    for (const auto& source : {"macro Flat { value: 1" +
                                   [] {
                                       std::string s;
                                       for (int i = 0; i < 4096; ++i) {
                                           s += "+1";
                                       }
                                       return s;
                                   }() +
                                   "; }",
                               "macro Elifs { value: 1; fn f() { if false {}" +
                                   [] {
                                       std::string s;
                                       for (int i = 0; i < 512; ++i) {
                                           s += " elif false {}";
                                       }
                                       return s;
                                   }() +
                                   "} }"}) {
        preprocessor::MacroEnvironment environment;
        bool failed = false;
        try {
            define(environment, source);
        } catch (const preprocessor::MacroError&) {
            failed = true;
        }
        check(failed, "iteratively constructed deep AST accepted");
    }
    preprocessor::MacroEnvironment environment;
    std::size_t cursor = 0;
    bool failed = false;
    try {
        environment.expand({}, cursor, "empty.optic");
    } catch (const preprocessor::MacroError&) {
        failed = true;
    }
    check(failed, "empty API token stream accepted");
}
} // namespace

int main() {
    transactions();
    malformed_input();
}
