/** @file preprocessor.cpp
 * @brief Module loading and token preprocessing with source-preserving imports.
 */
#include "preprocessor.hpp"
#include "compiler/driver/compiler_instance.hpp"
#include "compiler/lexer/lexer.hpp"

#include <fstream>
#include <iterator>
#include <iostream>

namespace optic::compiler::preprocessor {
using namespace driver;
using lexer::TokenType;

void Preprocessor::trace(const std::string& message) const {
    if (context.config().mode == CompilerMode::Debug)
        std::cout << "[preprocessor] " << message << '\n';
}

void Preprocessor::error(const Module& module, const lexer::token_t& token, const std::string& message) {
    trace("Failed at " + module.get_path() + ":" + std::to_string(token.line) + ":" +
          std::to_string(token.column) + ": " + message);
    context.diagnostics().report(
        {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column},
        "Error: " + message, "", "", DiagnosticsLevel::Error);
}

ModuleID Preprocessor::load(const std::filesystem::path& path, const SourceLocation& loc) {
    auto& graph = context.modules();
    if (const auto existing = graph.find_module(path); existing != invalid_module_id) {
        trace("Reusing module [" + std::to_string(existing) + "]: " + graph.get_module(existing)->get_path());
        return existing;
    }
    auto fail = [&](const std::string& message) {
        trace("Load failed for " + path.generic_string() + ": " + message);
        context.diagnostics().report(loc, "Error: " + message, "", "", DiagnosticsLevel::Error);
        return invalid_module_id;
    };
    if (path.extension() != OPTIC_EXTENSION)
        return fail("'" + path.generic_string() + "' is not a valid Optic source file; expected '.optic'.");
    std::error_code status_error;
    if (!std::filesystem::is_regular_file(path, status_error))
        return fail("Cannot open module '" + path.generic_string() + "': expected a regular source file.");
    std::ifstream file(path, std::ios::binary);
    if (!file) return fail("Cannot open module '" + path.generic_string() + "'.");
    std::string buffer{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) return fail("Cannot read module '" + path.generic_string() + "'.");
    auto module = std::make_unique<Module>(ModuleGraph::path_key(path));
    module->set_buffer(std::move(buffer));
    const auto id = graph.add_module(std::move(module));
    trace("Loaded module [" + std::to_string(id) + "]: " + graph.get_module(id)->get_path() +
          " (" + std::to_string(graph.get_module(id)->get_buffer().size()) + " bytes)");
    return id;
}

bool Preprocessor::process(ModuleID id, std::size_t depth) {
    auto& graph = context.modules();
    auto& module = *graph.get_module(id);
    if (module.state() == ModuleState::Ready) {
        trace("Already processed module [" + std::to_string(id) + "]: " + module.get_path());
        return true;
    }
    if (module.state() == ModuleState::Failed) {
        trace("Already failed module [" + std::to_string(id) + "]: " + module.get_path());
        return false;
    }
    if (depth >= 256) {
        error(module, {TokenType::Import, "import", 1, 1}, "Import nesting exceeds 256 modules.");
        module.set_state(ModuleState::Failed);
        return false;
    }
    trace("Processing module [" + std::to_string(id) + "] at import depth " + std::to_string(depth) + ": " + module.get_path());
    module.set_state(ModuleState::Processing);
    const auto errors_before = context.diagnostics().error_count();
    std::vector<lexer::token_t> input;
    lexer::Lexer(module, context.diagnostics()).tokenize(input);
    lexical_tokens += input.size() - 1;
    trace("Lexed module [" + std::to_string(id) + "]: " + std::to_string(input.size() - 1) + " tokens (excluding EOF)");
    auto& output = module.get_tokens();
    output.clear();
    if (context.diagnostics().error_count() != errors_before) {
        output = std::move(input);
        module.set_state(ModuleState::Failed);
        trace("Lexing failed for module [" + std::to_string(id) + "]: " + module.get_path());
        return false;
    }
    unsigned braces = 0;
    for (std::size_t cursor = 0; cursor + 1 < input.size();) {
        const auto token = input[cursor++];
        if (token.type == TokenType::Import) {
            ++import_requests;
            const auto start = cursor;
            while (cursor + 1 < input.size() && input[cursor].line == token.line &&
                   input[cursor].type != TokenType::Semicolon) ++cursor;
            const auto end = cursor;
            if (cursor + 1 < input.size() && input[cursor].type == TokenType::Semicolon) ++cursor;
            if (braces != 0) { error(module, token, "Imports are only allowed at module scope."); continue; }
            std::filesystem::path path;
            if (end == start + 1 && input[start].type == TokenType::StringLiteral) {
                const auto& name = input[start].value;
                if (name.empty() || name.find('\0') != std::string::npos) {
                    error(module, token, "Invalid empty or NUL-containing import path."); continue;
                }
                path = name;
            } else {
                // Bare names use dotted module components: import math.vector
                bool expect_name = true, valid = start != end;
                for (auto i = start; i < end; ++i) {
                    if (expect_name && input[i].type == TokenType::Identifier) path /= input[i].value;
                    else if (expect_name || input[i].type != TokenType::Point) { valid = false; break; }
                    expect_name = !expect_name;
                }
                if (!valid || expect_name) { error(module, token, "Expected a quoted path or dotted module name after 'import'."); continue; }
            }
            if (!path.has_extension()) path += OPTIC_EXTENSION;
            if (path.is_relative()) path = std::filesystem::path(module.get_path()).parent_path() / path;
            trace("Resolving import at " + module.get_path() + ":" + std::to_string(token.line) + ":" +
                  std::to_string(token.column) + " -> " + ModuleGraph::path_key(path));
            const auto dependency = load(path, {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column});
            if (dependency == invalid_module_id) continue;
            if (!graph.add_dependency(id, dependency)) {
                error(module, token, "Import cycle: '" + module.get_path() + "' -> '" + graph.get_module(dependency)->get_path() + "'.");
                continue;
            }
            if (!process(dependency, depth + 1)) {
                error(module, token, "Imported module failed preprocessing: '" + graph.get_module(dependency)->get_path() + "'.");
            } else {
                ++imports_completed;
                trace("Imported " + graph.get_module(dependency)->get_path() + " from " + module.get_path() +
                      " successfully. (edge [" + std::to_string(id) + "] -> [" + std::to_string(dependency) + "])");
            }
            continue;
        }
        if (token.type == TokenType::Macro) {
            error(module, token, "Macros are currently disabled.");
            break;
        }
        if (token.type == TokenType::LeftBrace) ++braces;
        if (token.type == TokenType::RightBrace && braces != 0) --braces;
        output.push_back(token);
    }
    output.push_back(input.back());
    const bool success = context.diagnostics().error_count() == errors_before;
    module.set_state(success ? ModuleState::Ready : ModuleState::Failed);
    trace(std::string(success ? "Processed" : "Failed") + " module [" + std::to_string(id) + "]: " +
          std::to_string(output.size() - 1) + " output tokens (excluding EOF), " +
          std::to_string(module.get_dependencies().size()) + " dependencies");
    return success;
}

bool Preprocessor::run(const std::vector<std::string>& input_files) {
    const auto errors_before = context.diagnostics().error_count();
    import_requests = imports_completed = lexical_tokens = 0;
    trace("Starting preprocessing: " + std::to_string(input_files.size()) + " input files; imports and module analysis active; macros disabled.");
    for (const auto& path : input_files) {
        const auto id = load(path, {SourceKind::Stdin, "", "", 0, 0});
        if (id != invalid_module_id) process(id, 0);
    }
    if (!context.modules().topological_sort()) {
        context.diagnostics().report({}, "Error: Invalid module dependency graph.", "", "", DiagnosticsLevel::Error);
    }
    const bool success = context.diagnostics().error_count() == errors_before;
    if (context.config().mode == CompilerMode::Debug) {
        std::cout << "\n----- PREPROCESSOR SUMMARY -----\n"
                  << "Result: " << (success ? "success" : "failed") << '\n'
                  << "Input files: " << input_files.size() << '\n'
                  << "Loaded modules: " << context.modules().size() << '\n'
                  << "Import requests: " << import_requests << '\n'
                  << "Successful imports: " << imports_completed << '\n'
                  << "Failed imports: " << import_requests - imports_completed << '\n'
                  << "Lexical tokens (excluding EOF): " << lexical_tokens << '\n'
                  << "New errors: " << context.diagnostics().error_count() - errors_before << '\n';
    }
    return success;
}
} // namespace optic::compiler::preprocessor
