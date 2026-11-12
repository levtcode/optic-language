/** @file preprocessor.hpp
 * @brief Token preprocessing over the driver's source modules.
 */
#pragma once

#include "compiler/driver/module.hpp"

namespace optic::compiler::driver { class CompilerContext; }
namespace optic::compiler::preprocessor {
/// Produces one parser token stream per Module, with dependency-first traversal.
/// A preprocessing session owns no second copy of the sources or module graph.
class Preprocessor {
public:
    explicit Preprocessor(driver::CompilerContext& context) : context(context) {}
    bool run(const std::vector<std::string>& input_files);

private:
    driver::CompilerContext& context;
    std::size_t import_requests = 0;
    std::size_t imports_completed = 0;
    std::size_t lexical_tokens = 0;
    void trace(const std::string& message) const;
    driver::ModuleID load(const std::filesystem::path& path, const driver::SourceLocation& loc);
    bool process(driver::ModuleID id, std::size_t depth);
    void error(const driver::Module& module, const lexer::token_t& token, const std::string& message);
};
} // namespace optic::compiler::preprocessor
