/**
 * @file args.hpp
 * @brief Compiler argument inspection and typed option parsing.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#pragma once

#include "compiler/driver/compiler_instance.hpp"

#include <string>
#include <string_view>

namespace optic::compiler::driver {

void print_args_files(const std::vector<std::string>&);
void print_args_options(const std::unordered_map<std::string, ArgValue>&);
void print_args_flags(const std::unordered_map<std::string, bool>&);
void print_args(const CompilerConfig&);

ArgValue parse_value(const std::string_view& option_name, size_t* index);
int match_compiler_arg(const std::string_view& arg, size_t* index);
bool same_arg_type(const ArgValue& a, const ArgValue& b);

} // namespace optic::compiler::driver
