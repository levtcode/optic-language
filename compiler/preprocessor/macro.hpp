/** @file macro.hpp
 * @brief Typed macro definitions, module environments and expansion diagnostics.
 */
#pragma once

#include "compiler/driver/diagnostics_engine.hpp"
#include "compiler/lexer/tokens.hpp"

#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <vector>

namespace optic::compiler::preprocessor {
class MacroError : public std::runtime_error {
public:
    driver::SourceLocation location;

    MacroError(driver::SourceLocation location, const std::string& message)
        : std::runtime_error(message),
          location(std::move(location)) {}
};

/// Definitions are immutable; values and expressions are copied at import boundaries.
/// All evaluation is bounded and has no filesystem, process or runtime-program access.
class MacroEnvironment {
public:
    MacroEnvironment();
    ~MacroEnvironment();
    MacroEnvironment(const MacroEnvironment&);
    MacroEnvironment& operator=(const MacroEnvironment&);
    MacroEnvironment(MacroEnvironment&&) noexcept;
    MacroEnvironment& operator=(MacroEnvironment&&) noexcept;

    void print(std::ostream& out) const;
    bool contains(const std::string& name) const;
    void
    define(const std::vector<lexer::token_t>& input, std::size_t& cursor, const std::string& file);
    void import(const MacroEnvironment& dependency, const driver::SourceLocation& location);
    /// cursor points at a known macro name, and advances past the complete invocation.
    std::vector<lexer::token_t>
    expand(const std::vector<lexer::token_t>& input, std::size_t& cursor, const std::string& file);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace optic::compiler::preprocessor
