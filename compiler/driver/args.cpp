/**
 * @file args.cpp
 * @brief Compiler argument inspection and typed option parsing.
 *
 * C++ declarations and definitions belong to namespace optic::compiler::driver.
 */

#include "compiler/driver/args.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>
#include <sstream>

namespace optic::compiler::driver {

namespace {
constexpr std::size_t minimum_name_width = 20;

std::string option_value(const ArgValue& value) {
    std::ostringstream out;
    std::visit(
        [&out](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                out << (item ? "true" : "false");
            } else {
                out << item;
            }
        },
        value);
    return out.str();
}

void print_settings(const std::string& heading,
                    const std::string& value_heading,
                    const std::map<std::string, std::string>& settings) {
    auto name_width = minimum_name_width;
    for (const auto& [name, value] : settings) {
        name_width = std::max(name_width, name.size());
    }

    std::cout << "\n  " << heading << "\n"
              << "    Name" << std::string(name_width - 4, ' ') << "  " << value_heading << "\n"
              << "    " << std::string(name_width, '-') << "  "
              << std::string(value_heading == "State" ? 8 : value_heading.size(), '-') << "\n";
    for (const auto& [name, value] : settings) {
        std::cout << "    " << name << std::string(name_width - name.size(), ' ') << "  " << value
                  << "\n";
    }
}
} // namespace

void print_args_files(const std::vector<std::string>& files) {
    std::cout << "  Input files\n";
    for (const auto& file : files) {
        std::cout << "    " << file << "\n";
    }
}

void print_args_options(const std::unordered_map<std::string, ArgValue>& options) {
    std::map<std::string, std::string> rows;
    for (const auto& [name, value] : options) {
        rows.emplace(name, option_value(value));
    }
    print_settings("Options", "Value", rows);
}

void print_args_flags(const std::unordered_map<std::string, bool>& flags) {
    std::map<std::string, std::string> rows;
    for (const auto& [name, enabled] : flags) {
        rows.emplace(name, enabled ? "enabled" : "disabled");
    }
    print_settings("Flags", "State", rows);
    std::cout << "\n";
}

void print_args(const CompilerConfig& config) {
    std::cout << "Compiler configuration\n\n";
    print_args_files(config.input_files);
    print_args_options(config.options);
    print_args_flags(config.flags);
}

int match_compiler_arg(const std::string_view& arg, size_t* index) {
    if (arg.empty() || arg[0] != '-') {
        return -1;
    }

    size_t i = 0;
    while (i < arg.size() && arg[i] != '=') {
        i++;
    }

    *index = i;
    if (i++ != arg.size()) {
        return 1;
    } else {
        return 0;
    }
}

bool same_arg_type(const ArgValue& a, const ArgValue& b) {
    return std::visit(
        []<typename A, typename B>(const A&, const B&) { return std::is_same_v<A, B>; }, a, b);
}

ArgValue parse_value(const std::string_view& option_name, size_t* index) {
    if (*index >= option_name.size()) {
        *index = std::string::npos;
        return std::string{};
    }
    std::string_view value = option_name.substr(*index + 1);

    if (value.empty()) {
        *index = std::string::npos;
        return std::string{};
    }

    if (value == "true") {
        return true;
    }

    if (value == "false") {
        return false;
    }

    int integer_value{};

    auto [int_ptr, int_ec] =
        std::from_chars(value.data(), value.data() + value.size(), integer_value);

    if (int_ec == std::errc{} && int_ptr == value.data() + value.size()) {
        return integer_value;
    }

    double floating_value{};
    const auto [float_ptr, float_ec] =
        std::from_chars(value.data(), value.data() + value.size(), floating_value);
    if (float_ec == std::errc{} && float_ptr == value.data() + value.size()) {
        return floating_value;
    }
    return std::string{value};
}

} // namespace optic::compiler::driver
