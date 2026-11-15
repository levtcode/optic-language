/** @file imports.cpp
 * @brief Import syntax and dependency resolution, separate from module traversal.
 */
#include "compiler/driver/compiler_instance.hpp"
#include "compiler/preprocessor/preprocessor.hpp"

namespace optic::compiler::preprocessor {
using namespace driver;
using lexer::TokenType;

bool Preprocessor::parse_import_name(std::span<const lexer::token_t> tokens,
                                     std::size_t& cursor,
                                     ImportRequest& request) {
    if (tokens[cursor].type == TokenType::StringLiteral) {
        request.name = tokens[cursor++].value;

        if (request.name.empty() || request.name.find('\0') != std::string::npos) {
            return false;
        }

        request.path = request.name;
        return true;
    }

    if (tokens[cursor].type != TokenType::Identifier) {
        return false;
    }

    request.name = tokens[cursor].value;
    request.path /= tokens[cursor++].value;

    while (cursor < tokens.size() && tokens[cursor].type == TokenType::Point) {
        ++cursor;

        if (cursor == tokens.size() || tokens[cursor].type != TokenType::Identifier) {
            return false;
        }

        request.name += "." + tokens[cursor].value;
        request.path /= tokens[cursor++].value;
    }

    return true;
}

bool Preprocessor::parse_import_requests(std::span<const lexer::token_t> tokens,
                                         std::vector<ImportRequest>& requests) {
    std::size_t cursor = 0;

    while (cursor < tokens.size()) {
        ImportRequest request;

        if (!parse_import_name(tokens, cursor, request)) {
            return false;
        }

        if (cursor < tokens.size() && tokens[cursor].type == TokenType::As) {
            ++cursor;

            if (cursor == tokens.size() || tokens[cursor].type != TokenType::Identifier) {
                return false;
            }

            request.alias = tokens[cursor++].value;
        }

        requests.push_back(std::move(request));

        if (cursor == tokens.size()) {
            return true;
        }

        if (tokens[cursor++].type != TokenType::Comma || cursor == tokens.size()) {
            return false;
        }
    }

    return false;
}

void Preprocessor::process_import_statement(ModuleID id,
                                            const lexer::token_t& token,
                                            std::span<const lexer::token_t> arguments,
                                            std::size_t depth) {
    std::vector<ImportRequest> requests;
    const bool valid = parse_import_requests(arguments, requests);

    // Failed statements retain the original accounting of completed list entries.
    import_requests += requests.empty() ? 1 : requests.size();

    if (!valid) {
        error(*context.modules().get_module(id),
              token,
              "Expected quoted paths or dotted module names, optional 'as' names and "
              "comma-separated imports.");
        return;
    }

    for (const auto& request : requests) {
        resolve_import(id, token, request, depth);
    }
}

void Preprocessor::resolve_import(ModuleID id,
                                  const lexer::token_t& token,
                                  const ImportRequest& request,
                                  std::size_t depth) {
    auto& graph = context.modules();
    auto& module = *graph.get_module(id);
    auto path = request.path;

    if (!path.has_extension()) {
        path += OPTIC_EXTENSION;
    }

    if (path.is_relative()) {
        path = std::filesystem::path(module.get_path()).parent_path() / path;
    }

    trace("Resolving import at " + module.get_path() + ":" + std::to_string(token.line) + ":" +
          std::to_string(token.column) + " -> " + ModuleGraph::path_key(path));
    const auto dependency =
        load(path, {SourceKind::BufferNoAST, module.get_path(), "", token.line, token.column});

    if (dependency == invalid_module_id) {
        return;
    }

    if (!graph.add_dependency(id, dependency)) {
        error(module,
              token,
              "Import cycle: '" + module.get_path() + "' -> '" +
                  graph.get_module(dependency)->get_path() + "'.");
        return;
    }

    if (!process(dependency, depth + 1)) {
        error(module,
              token,
              "Imported module failed preprocessing: '" + graph.get_module(dependency)->get_path() +
                  "'.");
        return;
    }

    ++imports_completed;
    module.record_import({dependency, request.name, request.alias, token.line, token.column});

    trace("Imported " + graph.get_module(dependency)->get_path() + " from " + module.get_path() +
          " successfully. (edge [" + std::to_string(id) + "] -> [" + std::to_string(dependency) +
          "])" + (request.alias.empty() ? "" : " as " + request.alias));
}
} // namespace optic::compiler::preprocessor
