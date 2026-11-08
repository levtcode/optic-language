/** @file preprocessor.cpp
 * @brief Module loading and token preprocessing with source-preserving imports.
 */
#include "preprocessor.hpp"
#include "compiler/driver/compiler_instance.hpp"
#include "compiler/lexer/lexer.hpp"
#include <fstream>
#include <iterator>

namespace optic::compiler::preprocessor {
using namespace driver;
using lexer::TokenType;

void Preprocessor::error(const Module& module, const lexer::token_t& token, const std::string& message) {
    context.diagnostics().report(
        {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column},
        "Error: " + message, "", "", DiagnosticsLevel::Error);
}

ModuleID Preprocessor::load(const std::filesystem::path& path, const SourceLocation& loc) {
    auto& graph = context.modules();
    if (const auto existing = graph.find_module(path); existing != invalid_module_id) return existing;
    auto fail = [&](const std::string& message) {
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
    return graph.add_module(std::move(module));
}

bool Preprocessor::process(ModuleID id, std::size_t depth) {
    auto& graph = context.modules();
    auto& module = *graph.get_module(id);
    if (module.state() == ModuleState::Ready) return true;
    if (module.state() == ModuleState::Failed) return false;
    if (depth >= 256) {
        error(module, {TokenType::Import, "import", 1, 1}, "Import nesting exceeds 256 modules.");
        module.set_state(ModuleState::Failed);
        return false;
    }
    module.set_state(ModuleState::Processing);
    const auto errors_before = context.diagnostics().error_count();
    std::vector<lexer::token_t> input;
    lexer::Lexer(module, context.diagnostics()).tokenize(input);
    auto& output = module.get_tokens();
    output.clear();
    if (context.diagnostics().error_count() != errors_before) {
        output = std::move(input);
        module.set_state(ModuleState::Failed);
        return false;
    }
    auto& macros = macro_environments.try_emplace(id).first->second;
    unsigned braces = 0;
    try {
        for (std::size_t cursor = 0; cursor + 1 < input.size();) {
            const auto token = input[cursor++];
            if (token.type == TokenType::Import) {
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
                const auto dependency = load(path, {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column});
                if (dependency == invalid_module_id) continue;
                if (!graph.add_dependency(id, dependency)) {
                    error(module, token, "Import cycle: '" + module.get_path() + "' -> '" + graph.get_module(dependency)->get_path() + "'.");
                    continue;
                }
                if (!process(dependency, depth + 1)) {
                    error(module, token, "Imported module failed preprocessing: '" + graph.get_module(dependency)->get_path() + "'.");
                } else macros.import(macro_environments.at(dependency),
                    {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column});
                continue;
            }
            if (token.type == TokenType::Macro) {
                if (braces != 0) throw MacroError(
                    {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column},
                    "Macros may only be declared at module scope.");
                macros.define(input, cursor, module.get_path());
                continue;
            }
            if (token.type == TokenType::Identifier && macros.contains(token.value)) {
                --cursor;
                auto generated = macros.expand(input, cursor, module.get_path());
                for (const auto& item : generated) {
                    if (item.type == TokenType::LeftBrace) ++braces;
                    if (item.type == TokenType::RightBrace && braces != 0) --braces;
                }
                output.insert(output.end(), std::make_move_iterator(generated.begin()), std::make_move_iterator(generated.end()));
                continue;
            }
            if (token.type == TokenType::LeftBrace) ++braces;
            if (token.type == TokenType::RightBrace && braces != 0) --braces;
            output.push_back(token);
        }
    } catch (const MacroError& failure) {
        context.diagnostics().report(failure.location, "Error: " + std::string(failure.what()), "", "", DiagnosticsLevel::Error);
    }
    output.push_back(input.back());
    const bool success = context.diagnostics().error_count() == errors_before;
    module.set_state(success ? ModuleState::Ready : ModuleState::Failed);
    return success;
}

bool Preprocessor::run(const std::vector<std::string>& input_files) {
    const auto errors_before = context.diagnostics().error_count();
    for (const auto& path : input_files) {
        const auto id = load(path, {SourceKind::Stdin, "", "", 0, 0});
        if (id != invalid_module_id) process(id, 0);
    }
    if (!context.modules().topological_sort()) {
        context.diagnostics().report({}, "Error: Invalid module dependency graph.", "", "", DiagnosticsLevel::Error);
    }
    return context.diagnostics().error_count() == errors_before;
}
} // namespace optic::compiler::preprocessor
