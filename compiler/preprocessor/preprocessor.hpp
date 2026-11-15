/** @file preprocessor.hpp
 * @brief Token preprocessing over the driver's source modules.
 */
#pragma once

#include "compiler/driver/module.hpp"

#include <span>

namespace optic::compiler::driver {
class CompilerContext;
}

namespace optic::compiler::preprocessor {
/// Produces one parser token stream per Module, with dependency-first traversal.
/// A preprocessing session owns no second copy of the sources or module graph.
class Preprocessor {
public:
    explicit Preprocessor(driver::CompilerContext& context)
        : context(context) {}

    bool run(const std::vector<std::string>& input_files);

private:
    struct ImportRequest {
        std::string name;
        std::string alias;
        std::filesystem::path path;
    };

    static constexpr std::size_t max_import_depth = 256;

    driver::CompilerContext& context;
    std::size_t import_requests = 0;
    std::size_t imports_completed = 0;
    std::size_t lexical_tokens = 0;

    void trace(const std::string& message) const;
    void error(const driver::Module& module, const lexer::token_t& token, const std::string& message);

    driver::ModuleID load(const std::filesystem::path& path, const driver::SourceLocation& loc);
    bool process(driver::ModuleID id, std::size_t depth);
    bool tokenize_module(driver::ModuleID id, std::vector<lexer::token_t>& input);
    void filter_tokens(driver::ModuleID id, std::span<const lexer::token_t> input, std::size_t depth);

    static bool parse_import_name(std::span<const lexer::token_t> tokens,
                                  std::size_t& cursor,
                                  ImportRequest& request);
    static bool parse_import_requests(std::span<const lexer::token_t> tokens,
                                      std::vector<ImportRequest>& requests);
    void process_import_statement(driver::ModuleID id,
                                  const lexer::token_t& token,
                                  std::span<const lexer::token_t> arguments,
                                  std::size_t depth);
    void resolve_import(driver::ModuleID id,
                        const lexer::token_t& token,
                        const ImportRequest& request,
                        std::size_t depth);
};
} // namespace optic::compiler::preprocessor
