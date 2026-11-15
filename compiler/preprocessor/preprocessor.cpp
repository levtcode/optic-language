/** @file preprocessor.cpp
 * @brief Module loading and token preprocessing with source-preserving imports.
 */
#include "preprocessor.hpp"
#include "compiler/driver/compiler_instance.hpp"
#include "compiler/lexer/lexer.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace optic::compiler::preprocessor {
using namespace driver;
using lexer::TokenType;

void Preprocessor::trace(const std::string& message) const {
    if (context.config().mode == CompilerMode::Debug) {
        std::cout << "[preprocessor] " << message << '\n';
    }
}

void Preprocessor::error(const Module& module, const lexer::token_t& token, const std::string& message) {
    trace("Failed at " + module.get_path() + ":" + std::to_string(token.line) + ":" +
          std::to_string(token.column) + ": " + message);

    const SourceLocation loc{SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column};
    context.diagnostics().report(
        loc,
        "Error: " + message,
        "",
        "",
        DiagnosticsLevel::Error,
        SourceLocationRange::from_text(loc, token.value));
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

    if (path.extension() != OPTIC_EXTENSION) {
        return fail("'" + path.generic_string() + "' is not a valid Optic source file; expected '.optic'.");
    }

    std::error_code status_error;

    if (!std::filesystem::is_regular_file(path, status_error)) {
        return fail("Cannot open module '" + path.generic_string() + "': expected a regular source file.");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        return fail("Cannot open module '" + path.generic_string() + "'.");
    }

    std::string buffer{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    if (file.bad()) {
        return fail("Cannot read module '" + path.generic_string() + "'.");
    }

    auto module = std::make_unique<Module>(ModuleGraph::path_key(path));
    module->set_buffer(std::move(buffer));

    const auto id = graph.add_module(std::move(module));
    trace("Loaded module [" + std::to_string(id) + "]: " + graph.get_module(id)->get_path() + " (" +
          std::to_string(graph.get_module(id)->get_buffer().size()) + " bytes)");

    return id;
}

bool Preprocessor::process(ModuleID id, std::size_t depth) {
    auto& module = *context.modules().get_module(id);

    if (module.state() == ModuleState::Ready) {
        trace("Already processed module [" + std::to_string(id) + "]: " + module.get_path());
        return true;
    }

    if (module.state() == ModuleState::Failed) {
        trace("Already failed module [" + std::to_string(id) + "]: " + module.get_path());
        return false;
    }

    if (depth >= max_import_depth) {
        error(module, {TokenType::Import, "import", 1, 1}, "Import nesting exceeds 256 modules.");
        module.set_state(ModuleState::Failed);
        return false;
    }

    trace("Processing module [" + std::to_string(id) + "] at import depth " +
          std::to_string(depth) + ": " + module.get_path());

    module.set_state(ModuleState::Processing);

    const auto errors_before = context.diagnostics().error_count();
    std::vector<lexer::token_t> input;

    tokenize_module(id, input);

    module.clear_imports();
    filter_tokens(id, input, depth);
    module.mark_parser_input_ready();

    const bool success = context.diagnostics().error_count() == errors_before;
    module.set_state(success ? ModuleState::Ready : ModuleState::Failed);

    trace(std::string(success ? "Processed" : "Failed") + " module [" + std::to_string(id) + "]: " +
          std::to_string(module.get_tokens().size() - 1) + " output tokens (excluding EOF), " +
          std::to_string(module.get_dependencies().size()) + " dependencies");

    return success;
}

void Preprocessor::tokenize_module(ModuleID id, std::vector<lexer::token_t>& input) {
    auto& module = *context.modules().get_module(id);
    const auto errors_before = context.diagnostics().error_count();

    lexer::Lexer(module, context.diagnostics()).tokenize_layout(input);
    lexical_tokens += input.size() - 1;

    trace("Lexed module [" + std::to_string(id) + "]: " + std::to_string(input.size() - 1) +
          " tokens (excluding EOF)");

    auto& output = module.get_tokens();
    output.clear();

    if (context.diagnostics().error_count() != errors_before) {
        trace("Lexing reported errors for module [" + std::to_string(id) +
              "]; retaining recoverable tokens for parsing.");
    }
}

void Preprocessor::filter_tokens(ModuleID id, std::span<const lexer::token_t> input, std::size_t depth) {
    auto& module = *context.modules().get_module(id);
    auto& output = module.get_tokens();
    
    unsigned braces = 0;
    unsigned parens = 0;
    unsigned brackets = 0;

    // Preserve the final EOF even when a directive fails or macro processing stops.
    for (std::size_t cursor = 0; cursor + 1 < input.size();) {
        const auto& token = input[cursor++];

        if (token.type == TokenType::Import) {
            const auto start = cursor;

            while (cursor + 1 < input.size() && input[cursor].line == token.line &&
                   input[cursor].type != TokenType::Semicolon &&
                   input[cursor].type != TokenType::Newline) {
                ++cursor;
            }

            const auto arguments = input.subspan(start, cursor - start);

            if (cursor + 1 < input.size() && input[cursor].type == TokenType::Semicolon) {
                ++cursor;
            }

            if (braces != 0 || parens != 0 || brackets != 0) {
                ++import_requests;
                error(module, token, "Imports are only allowed at module scope.");
                continue;
            }

            process_import_statement(id, token, arguments, depth);
            continue;
        }

        if (token.type == TokenType::Macro) {
            error(module, token, "Macros are currently disabled.");
            break;
        }

        if (token.type == TokenType::LeftBrace) {
            ++braces;
        }

        if (token.type == TokenType::RightBrace && braces != 0) {
            --braces;
        }

        if (token.type == TokenType::LeftParen) {
            ++parens;
        }

        if (token.type == TokenType::RightParen && parens != 0) {
            --parens;
        }

        if (token.type == TokenType::LeftBracket) {
            ++brackets;
        }

        if (token.type == TokenType::RightBracket && brackets != 0) {
            --brackets;
        }

        output.push_back(token);
    }

    output.push_back(input.back());
}

bool Preprocessor::run(const std::vector<std::string>& input_files) {
    const auto errors_before = context.diagnostics().error_count();
    import_requests = imports_completed = lexical_tokens = 0;

    std::cout << "\n---- PREPROCESSOR TRACING ----\n";
    trace("Starting preprocessing: " + std::to_string(input_files.size()) +
          " input files; imports and module analysis active; macros disabled.");

    for (const auto& path : input_files) {
        const auto id = load(path, {SourceKind::Stdin, "", "", 0, 0});

        if (id != invalid_module_id) {
            process(id, 0);
        }
    }

    std::cout << "\n";

    if (!context.modules().topological_sort()) {
        context.diagnostics().report(
            {}, "Error: Invalid module dependency graph.", "", "", DiagnosticsLevel::Error);
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
                  << "New errors: " << context.diagnostics().error_count() - errors_before << '\n'
                  << "\n";
    }

    return success;
}
} // namespace optic::compiler::preprocessor
