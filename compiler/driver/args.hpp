/* args.hpp */

#pragma once

#include "compiler/driver/compiler_instance.hpp"
#include <string>
#include <string_view>

namespace cli_tooling {
void print_args_files(std::vector<std::string>&);
void print_args_options(std::unordered_map<std::string, ArgValue>&);
void print_args_flags(std::unordered_map<std::string, bool>&);
void print_args(CompilerConfig&);

ArgValue parse_value(const std::string_view &option_name, size_t *index);
int match_compiler_arg(const std::string_view &arg, size_t *index);
bool same_arg_type(const ArgValue& a, const ArgValue& b);
}