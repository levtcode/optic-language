#pragma once
#include "compiler/ast/ast.hpp"
#include <iosfwd>

namespace optic::compiler::ast {
/// Deterministic tree representation for debug inspection and contract tests.
void dump(const ModuleAST& module, std::ostream& out);
} // namespace optic::compiler::ast
